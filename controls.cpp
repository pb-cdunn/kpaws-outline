// "Globals" (attached to WebService?)
class Globals {
    ProcessRegister preg;
    map<Sid, ProcessingDarkcal> dpreg;
    map<Sid, ProcessingLoadingcal> lpreg;
    map<Sid, ProcessingBasecaller> bpreg;
    map<Mid, ProcessingPpa> ppreg;
};


// thread-safe
class ProcessRegister {
    ProcessController* Find(int pid);
    ProcessController* Pop(int pid); // idempotent!
    vector<ProcessController*> All();

    map<int, ProcessController*> pcs;
    // Key is probably just PID.
    // But what if O/S reuses PIDs? Race-condition? Should we worry?
};

// thread-safe and reference-counted
class ProcessController {
    virtual ~ProcessController();
    void Stop(); // idempotent! Always called by dtor.

    int pid = -1;
};

~ProcessController::ProcessController()
{
    Stop();
}
void ProcessController::Stop()
{
    if (pid == -1) return;
    // Signal INT, wait, then signal KILL?
}

class StandardProcessController : public ProcessController {
    virtual ~StandardProcessController();
    bool Check() const;
    void StartHealthTimerThread();

    Health health; // reference-counted?
    HealthTimerThread hcThread;
    // logging?
    // dealing with stdout/stderr outputs?
};

void ~StandardProcessController::StandardProcessController() const
{
    hcThread.Stop(); // Will this work for a sleeping thread?
}
void StandardProcessController::Check() const
{
    // HealthEnum {Unknown, Pending, Ok, Unresponsive, Dead};
    return ((health.recent != Dead) && (health.recent != Unresponsive));
}
void StandardProcessController::StartHealthTimerThread()
{
    hcThread = CreateHealthTimerThread(&health);
    hcThread.Start();
}

class ProcessControllerForBasecaller : public ProcessController {
    virtual ~ProcessControllerForBasecaller();
    void StartWatcherThread();

    Thread watcherThread;
    BasecallerData data;
};

virtual ~ProcessControllerForBasecaller::ProcessControllerForBasecaller()
{
    watcherThread.Stop(); // Will this work for a blocked thread?
}
void ProcessControllerForBasecaller::StartWatcherThread() {
    watcherThread = CreateReadThreadForBasecaller(&health);
    watcherThread.Start();
}

class ProcessControllerForPpa : public ProcessController {
    PpaData data;
    // ...
};
// Darkcal ...
// Loadingcal ...

// We want ProcessController to be reference-counted.
// So we will have to switch between
//   shared_ptr<ProcessController>
// and (e.g.)
//   shared_ptr<ProcessControllerForBaseCaller>
// using
//   https://www.cplusplus.com/reference/memory/dynamic_pointer_cast/
// Is that a good idea?


BasecallerProcessController* SpawnProcessForBasecaller(cmd) {
    pc = ForkBasecaller(cmd); // TBD
    pc->StartWatcherThread(); // Use file-descriptor on pc.
    pc->pid = pc->health.GetPidWithTimeout(); // can throw
    pc->StartHealthTimerThread();
    return pc;
    // Note: pc must be ref-counted, so it will self-destruct on throw.
    // Health must be ref-counted too, so threads can update it after pc dtor.
    // We could consider a slightly different architecture though.
    // PC can be immutable after this factory finishes, I think.
}

//////////////////////////////////
// RESTful calls, each on a thread


// POST /socket/SID/basecaller/start
void StartBasecaller(sid, body) {
    BasecallerData bdata = CreateBasecallerData(body);
    BashString cmd = GenerateBashCmdForBasecaller(bdata);
    ProcessControllerForBasecaller* pc = SpawnProcessForBasecaller(cmd, bdata);
    preg.Register(pc->pid, pc); // Key would be UNIX PID.
    bdreg.Register(sid, pc);
}

// POST /postprimaries
void StartPpa(body) {
    PpaData pdata = CreatePpaData(body);
    BashString cmd = GenerateBashCmdForPpa(pdata);
    ProcessControllerForPpa* pc = SpawnProcessForPpa(cmd, pdata);
    pkey = preg.Register(pc->pid, pc); // Key would be UNIX PID.
    pdreg.Register(mid, pc);
}

// POST /postprimaries/MID/stop
void StopPpa(mid, body) {
    ppc = pdreg.Pop(mid);
    pc = preg.Pop(pdata->pkey);
    pc->Stop();
}

// GET /healthcheck (?)
void CheckAll() {
    for (pkey, pc : preg.All()) {
        ok = pc->Check();
        if (!ok) {
            preg.Pop(pkey);
            // Ignore result.
        }
        // When ref-counted pc goes out of scope, its
        // destructor should end its threads.
}


////////////////////////////////////////////////////////////
// Threads
// Each REST calls happens on a thread of course.
// Those will sometimes spawn others.

// This class already exists somewhere. Just a place-holder.
class Thread {
    virtual void Run();
    void Start(); // Create thread, then call Run().
};

// thread-safe, maybe ref-counted
class Health {
    Set(HealthEnum);
    SetNextTimeout(double);
    void SetPid(int);
    int GetPidWithTimeout(); // Wait for pid to be positive. Throw on timeout.

    HealthEnum {Unknown, Pending, Ok, Unresponsive, Dead};
    HealthEnum recent;
    double timeout;
    int pid; // technically could be elsewhere, but comes from same place as health
};

class HealthTimerThread : public Thread {
    virtual void Run();

    Health* health; // owned elsewhere?
};

void HealthTimerThread::Run() {
    do {
        health.Set(Pending);

        // Technically, we should sleep only the difference between timeout
        // and elapsed time since the timeout was set, but a bit extra is fine.
        sleep(health.timeout);

        // Can a sleeping thread be killed?
        // If not, this needs to be split into many short sleeps.
    } while(health.ok);
    // Do nothing when bad. Just stop going to Pending.
}

class ReadThreadForBasecaller : public Thread {
    virtual void Run();
    virtual ParsedReport ParseStatusReport(string);

    Health* health;
};

void ReadThreadForBasecaller::Run() {
    while (line = ReadFd()) {
        ParsedReportForBasecaller pr = ParseStatusReport(line);
        health->Set(pr.status);
        health->SetNextTimeout(pr.nextDelay);
    }
}

class ParsedReportForBasecaller {
    HealthEnum Health(); // Convert from specific Status to generic Health.
    double NextDelay();

    StatusEnum status;
    double delay;
};
