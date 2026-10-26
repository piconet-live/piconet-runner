// map-sample.cs: an external sampler for the whole-frame map. It changes nothing in
// the runner: it opens the running moderngekko-run.exe, and about once a millisecond suspends one of its threads,
// reads where it is, and lets it go. For the CPU thread it also reads the emulated machine at that instant: the guest
// stack (so a sample is known to be inside a task, the drawing, an interrupt handler), the task the game's task
// runner is in, the race frame counter and the emulated clock. For the video thread and every other thread it reads
// the instruction pointer and the return addresses found on the host stack.
//
// Build (the .NET Framework's compiler, on every Windows):
//   C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /optimize /unsafe /platform:x64 map-sample.cs
// Run:  map-sample.exe PID OUT.txt KEY=VALUE...
//   system=RVA   the runner's static Core::System instance (its link map: ?instance@...GetInstance@System@Core)
//   ppc=OFF      Core::System::Impl -> PowerPCManager (System::GetPPCState adds it)
//   timing=OFF   Core::System::Impl -> CoreTimingManager (System::GetCoreTiming adds it)
//   core=OFF     PowerPCManager -> the CPU core's pointer (PowerPCManager::RunLoop reads it)
//   guest=OFF    StaticRecompCore -> its CPUState (StaticRecompCore::SyncIn writes it)
//   seconds=N    stop after N seconds (default 200); it also stops when the process ends or OUT.stop exists
//   others=N     every Nth tick the other threads are sampled too (default 4)
//   period=US    the mean time between two ticks in microseconds (default 600; each wait is 0.5 to 1.5 of it)
// The offsets belong to one build of the runner (read out of its exe by disassembling the functions named).
// Output, one record a line (times: the performance counter in microseconds, the pacing log's clock):
//   M base size path                          a loaded module
//   N tid name                                a thread's name
//   G key value                               what was found (guest RAM, the core)
//   C time rip r1 lr pc taskfn race_frame global_timer slice downcount sane guest_downcount budget | guest return
//     addresses, innermost first | host stack words that point into the exe or the module
//   V time rip | host stack words            the video thread
//   O tid time rip | host stack words        another thread
//   T time tid cycles                        a thread's CPU time so far (the processor's cycle counter)
// SPDX-License-Identifier: GPL-3.0-or-later
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

unsafe class MapSample
{
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenThread(uint access, bool inherit, uint tid);
    [DllImport("kernel32.dll")] static extern uint SuspendThread(IntPtr thread);
    [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll")] static extern bool GetThreadContext(IntPtr thread, byte* context);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr process, ulong address, byte* buffer, UIntPtr size, out UIntPtr read);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle, uint ms);
    [DllImport("kernel32.dll")] static extern bool QueryThreadCycleTime(IntPtr thread, out ulong cycles);
    [DllImport("kernel32.dll")] static extern int GetThreadDescription(IntPtr thread, out IntPtr text);
    [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr p);
    [DllImport("kernel32.dll")] static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint pid);
    [DllImport("kernel32.dll")] static extern bool Thread32First(IntPtr snapshot, ref THREADENTRY32 entry);
    [DllImport("kernel32.dll")] static extern bool Thread32Next(IntPtr snapshot, ref THREADENTRY32 entry);
    [DllImport("psapi.dll")] static extern bool EnumProcessModulesEx(IntPtr process, [Out] IntPtr[] modules, uint bytes, out uint needed, uint filter);
    [DllImport("psapi.dll")] static extern bool GetModuleInformation(IntPtr process, IntPtr module, out MODULEINFO info, uint size);
    [DllImport("psapi.dll", CharSet = CharSet.Unicode)] static extern uint GetModuleFileNameExW(IntPtr process, IntPtr module, StringBuilder name, uint size);
    [DllImport("winmm.dll")] static extern uint timeBeginPeriod(uint ms);
    [DllImport("kernel32.dll")] static extern void Sleep(uint ms);
    [DllImport("kernel32.dll")] static extern IntPtr CreateWaitableTimerExW(IntPtr attributes, IntPtr name, uint flags, uint access);
    [DllImport("kernel32.dll")] static extern bool SetWaitableTimer(IntPtr timer, ref long due, int period, IntPtr routine, IntPtr arg, bool resume);

    [StructLayout(LayoutKind.Sequential)] struct THREADENTRY32 { public uint size, usage, tid, pid; public int basePri, deltaPri; public uint flags; }
    [StructLayout(LayoutKind.Sequential)] struct MODULEINFO { public IntPtr baseAddress; public uint size; public IntPtr entry; }

    static IntPtr process;
    static byte[] bufA = new byte[0x4000];

    static bool Read(ulong address, byte* to, int size)
    {
        UIntPtr got;
        return ReadProcessMemory(process, address, to, (UIntPtr)(uint)size, out got) && (int)got == size;
    }
    static ulong ReadU64(ulong address) { ulong v = 0; return Read(address, (byte*)&v, 8) ? v : 0; }
    static uint BE32(byte* p) { return ((uint)p[0] << 24) | ((uint)p[1] << 16) | ((uint)p[2] << 8) | p[3]; }

    class Module { public ulong baseAddress, size; public string path; }
    static List<Module> modules = new List<Module>();
    static void ListModules(StreamWriter o, HashSet<ulong> seen)
    {
        IntPtr[] handles = new IntPtr[2048]; uint needed;
        if (!EnumProcessModulesEx(process, handles, (uint)(handles.Length * 8), out needed, 3)) return;
        int n = Math.Min(handles.Length, (int)(needed / 8));
        for (int i = 0; i < n; i++)
        {
            ulong b = (ulong)handles[i].ToInt64();
            if (seen.Contains(b)) continue;
            MODULEINFO info; if (!GetModuleInformation(process, handles[i], out info, 24)) continue;
            StringBuilder name = new StringBuilder(600); GetModuleFileNameExW(process, handles[i], name, 600);
            seen.Add(b);
            Module m = new Module(); m.baseAddress = b; m.size = info.size; m.path = name.ToString(); modules.Add(m);
            o.WriteLine("M {0:x} {1:x} {2}", b, info.size, m.path);
        }
    }

    class ThreadInfo { public uint tid; public IntPtr handle; public string name = ""; public ulong lastCycles; public bool active; public int kind; }
    static Dictionary<uint, ThreadInfo> threads = new Dictionary<uint, ThreadInfo>();
    static void ListThreads(int pid, StreamWriter o)
    {
        IntPtr snap = CreateToolhelp32Snapshot(4, 0);
        THREADENTRY32 e = new THREADENTRY32(); e.size = (uint)sizeof(THREADENTRY32);
        if (Thread32First(snap, ref e))
            do
            {
                if (e.pid != (uint)pid || threads.ContainsKey(e.tid)) continue;
                IntPtr h = OpenThread(0x0002 | 0x0008 | 0x0040 | 0x0800, false, e.tid);  // suspend, get context, query, limited
                if (h == IntPtr.Zero) continue;
                ThreadInfo t = new ThreadInfo(); t.tid = e.tid; t.handle = h; threads[e.tid] = t;
            } while (Thread32Next(snap, ref e));
        CloseHandle(snap);
        foreach (ThreadInfo t in threads.Values)
        {
            IntPtr text;
            if (GetThreadDescription(t.handle, out text) >= 0 && text != IntPtr.Zero)
            {
                string name = Marshal.PtrToStringUni(text); LocalFree(text);
                if (name != t.name && name.Length > 0)
                {
                    t.name = name; o.WriteLine("N {0} {1}", t.tid, name);
                    t.kind = name == "CPU thread" ? 1 : name == "Video thread" ? 2 : 0;
                }
            }
        }
    }

    static double qpcScale = 1e6 / Stopwatch.Frequency;
    static ulong Now() { return (ulong)(Stopwatch.GetTimestamp() * qpcScale); }

    // What the sampler was asked (Options, its command line's one reader) and what it writes to.
    static int pid;
    static string outPath;
    static Dictionary<string, ulong> opt = new Dictionary<string, ulong>();
    static StreamWriter o;
    static HashSet<ulong> seenModules = new HashSet<ulong>();
    // Where the runner and the module are, and what was found in the runner (once a second, until found).
    static ulong exeBase, exeEnd, dllBase, dllEnd;
    static ulong impl, core, guest, ram, ramSize, ppc, timing;
    static uint manager, raceFrameAddress;
    // The buffers one sample reads into, made once.
    static byte* context, stack, g, gs, small;
    static uint* ras;
    static StringBuilder line = new StringBuilder(4096);

    // The command line: PID OUT.txt KEY=VALUE... (the head of this file), with the defaults.
    static void Options(string[] args)
    {
        pid = int.Parse(args[0]);
        outPath = args[1];
        opt["seconds"] = 200; opt["others"] = 4; opt["period"] = 600;
        for (int i = 2; i < args.Length; i++)
        {
            string[] kv = args[i].Split('=');
            opt[kv[0]] = kv[1].StartsWith("0x") ? Convert.ToUInt64(kv[1].Substring(2), 16) : ulong.Parse(kv[1]);
        }
    }

    static void Main(string[] args)
    {
        Options(args);
        process = OpenProcess(0x0010 | 0x0400 | 0x00100000, false, pid);  // read memory, query, synchronize
        if (process == IntPtr.Zero) { Console.Error.WriteLine("map-sample: cannot open process " + pid); Environment.Exit(2); }
        timeBeginPeriod(1);
        o = new StreamWriter(outPath, false, new UTF8Encoding(false), 1 << 22);
        ListModules(o, seenModules);
        exeBase = modules.Count > 0 ? modules[0].baseAddress : 0; exeEnd = modules.Count > 0 ? exeBase + modules[0].size : 0;

        context = (byte*)Marshal.AllocHGlobal(1232 + 32); context = (byte*)(((ulong)context + 15) & ~15UL);
        stack = (byte*)Marshal.AllocHGlobal(0x4000);
        g = (byte*)Marshal.AllocHGlobal(0x1000);
        gs = (byte*)Marshal.AllocHGlobal(0x1000);
        small = (byte*)Marshal.AllocHGlobal(0x400);
        ras = (uint*)Marshal.AllocHGlobal(48 * 4);
        Random random = new Random(12345);
        ulong start = Now(), end = start + opt["seconds"] * 1000000UL, nextSecond = start;
        long tick = 0; string stopFile = outPath + ".stop";
        IntPtr timer = CreateWaitableTimerExW(IntPtr.Zero, IntPtr.Zero, 2, 0x1F0003);  // high resolution (Windows 10 1803 on)

        while (true)
        {
            ulong now = Now();
            if (now >= nextSecond)
            {
                nextSecond = now + 1000000;
                if (WaitForSingleObject(process, 0) == 0 || now > end || File.Exists(stopFile)) break;
                EverySecond(now);
            }
            bool sampleOthers = (tick % (long)opt["others"]) == 0;
            foreach (ThreadInfo t in threads.Values)
            {
                if (t.kind == 0 && !(sampleOthers && t.active)) continue;
                Sample s = new Sample();
                if (Take(t, s)) Write(t, s);
            }
            tick++;
            // a wait of half to one and a half periods, drawn at random: never in step with the game's 16.68 ms
            long due = -(long)((0.5 + random.NextDouble()) * opt["period"] * 10);
            if (timer != IntPtr.Zero && SetWaitableTimer(timer, ref due, 0, IntPtr.Zero, IntPtr.Zero, false)) WaitForSingleObject(timer, 100);
            else Sleep(1);
        }
        ulong last = Now();
        foreach (ThreadInfo t in threads.Values) { ulong cycles; if (QueryThreadCycleTime(t.handle, out cycles)) o.WriteLine("T {0} {1} {2}", last, t.tid, cycles); }
        ListModules(o, seenModules);
        o.WriteLine("E {0} ticks {1}", last, tick);
        o.Close();
    }

    // Once a second: new modules and threads, each thread's CPU time, and what is not found yet in the runner.
    static void EverySecond(ulong now)
    {
        ListModules(o, seenModules);
        ListThreads(pid, o);
        foreach (ThreadInfo t in threads.Values)
        {
            ulong cycles; if (!QueryThreadCycleTime(t.handle, out cycles)) continue;
            t.active = cycles != t.lastCycles; t.lastCycles = cycles;
            o.WriteLine("T {0} {1} {2}", now, t.tid, cycles);
        }
        if (dllBase == 0)
            foreach (Module m in modules)
                if (m.path.EndsWith("_recomp.dll", StringComparison.OrdinalIgnoreCase)) { dllBase = m.baseAddress; dllEnd = dllBase + m.size; }
        if (guest == 0 && opt.ContainsKey("system")) FindGuest();
        if (guest != 0 && manager == 0) FindManager();
        o.Flush();
    }

    // The emulated machine's state and RAM, through the runner's static System instance and the five offsets.
    static void FindGuest()
    {
        impl = ReadU64(exeBase + opt["system"]);
        if (impl == 0) return;
        ppc = impl + opt["ppc"]; timing = impl + opt["timing"];
        core = ReadU64(ppc + opt["core"]);
        ulong candidate = core + opt["guest"];
        if (core != 0 && ReadU64(candidate + 0xD78) == core)
        {
            ram = ReadU64(candidate + 0xD80); ramSize = ReadU64(candidate + 0xD88) & 0xFFFFFFFF;
            if (ram != 0 && ramSize >= 0x1800000) { guest = candidate; o.WriteLine("G guest {0:x} ram {1:x} size {2:x} core {3:x} impl {4:x}", guest, ram, ramSize, core, impl); }
        }
    }

    // main.rel's base (0x800030C8), its task manager (0x8037544C when main.rel stands at 0x801BA660).
    static void FindManager()
    {
        if (Read(ram + 0x30C8, small, 4) && BE32(small) == 0x801BA660u && Read(ram + 0x37544C, small, 4))
        {
            uint m = BE32(small);
            if (m >= 0x80000000u && m - 0x80000000u < ramSize) { manager = m; raceFrameAddress = 0x801BA660u + 0x1BA040u + 0x50A4u; o.WriteLine("G manager {0:x}", manager); }
        }
    }

    // What one sample of one thread found.
    class Sample
    {
        public int stackBytes, chain;
        public ulong at, rip, rsp;
        public bool haveStack, haveGuest, haveTiming, havePpc;
        public uint r1, lr, pc, taskFn, raceFrame;
    }

    // One thread, suspended for as short a time as can be: its registers, its host stack and, for the CPU thread,
    // the guest's. False when the thread could not be suspended or read: nothing is written for it.
    static bool Take(ThreadInfo t, Sample s)
    {
        s.stackBytes = t.kind == 1 ? 0x800 : t.kind == 2 ? 0x3000 : 0x1000;
        if (SuspendThread(t.handle) == 0xFFFFFFFF) return false;
        s.at = Now();
        try
        {
            *(uint*)(context + 0x30) = 0x100001;  // CONTEXT_CONTROL
            if (!GetThreadContext(t.handle, context)) return false;
            s.rip = *(ulong*)(context + 0xF8); s.rsp = *(ulong*)(context + 0x98);
            s.haveStack = Read(s.rsp, stack, s.stackBytes);
            if (!s.haveStack) { s.stackBytes = (int)(0x1000 - (s.rsp & 0xFFF)); s.haveStack = s.stackBytes >= 8 && Read(s.rsp, stack, s.stackBytes); }
            if (t.kind == 1 && guest != 0)
            {
                s.haveGuest = Read(guest, g, 0xDA8);
                s.haveTiming = Read(timing, small, 0x120);
                s.havePpc = Read(ppc, small + 0x120, 0xF4);
                if (s.haveGuest)
                {
                    s.r1 = *(uint*)(g + 4); s.lr = *(uint*)(g + 0x284); s.pc = *(uint*)(g + 0x280);
                    GuestStack(s);
                    if (manager != 0) TaskAndFrame(s);
                }
            }
        }
        finally { ResumeThread(t.handle); }
        return true;
    }

    // The guest stack: [sp] = the caller's frame, [caller's frame + 4] = the return address into the caller.
    static void GuestStack(Sample s)
    {
        uint sp = s.r1; ulong page = 0; bool pageOk = false;
        for (int depth = 0; depth < 40; depth++)
        {
            uint off = sp - 0x80000000u;
            if (sp < 0x80000000u || off + 8 > ramSize || (sp & 3) != 0) break;
            ulong want = ram + (off & ~0x3FFu);
            if (want != page || !pageOk) { page = want; pageOk = Read(page, gs, 0x408); }
            if (!pageOk) break;
            byte* f = gs + (off & 0x3FF);
            uint back = BE32(f);
            if (depth > 0) ras[s.chain++] = BE32(f + 4);
            if (back <= sp) break;
            sp = back;
        }
    }

    // The task the game's task runner is in, and the race frame counter. The game has more than one task manager:
    // the one in use is read with every sample.
    static void TaskAndFrame(Sample s)
    {
        byte* w = small + 0x300;
        uint current = Read(ram + 0x37544C, w, 4) ? BE32(w) : 0;
        if (current >= 0x80000000u && current - 0x80000000u + 12 <= ramSize && Read(ram + (current - 0x80000000u) + 8, w, 4))
        {
            uint node = BE32(w);
            if (node >= 0x80000000u && node - 0x80000000u + 4 <= ramSize && Read(ram + (node - 0x80000000u), w, 4)) s.taskFn = BE32(w);
        }
        if (Read(ram + (raceFrameAddress - 0x80000000u), w, 4)) s.raceFrame = BE32(w);
    }

    // One sample's record (C, V or O: the head of this file), with the host stack words that point into the runner
    // or the module.
    static void Write(ThreadInfo t, Sample s)
    {
        line.Length = 0;
        if (t.kind == 1)
        {
            line.Append("C ").Append(s.at).Append(' ').Append(s.rip.ToString("x")).Append(' ').Append(s.r1.ToString("x")).Append(' ').Append(s.lr.ToString("x")).Append(' ')
                .Append(s.pc.ToString("x")).Append(' ').Append(s.taskFn.ToString("x")).Append(' ').Append(s.raceFrame);
            if (s.haveTiming && s.havePpc && s.haveGuest)
                line.Append(' ').Append(*(long*)small).Append(' ').Append(*(int*)(small + 8)).Append(' ').Append(*(int*)(small + 0x120 + 0xF0)).Append(' ')
                    .Append(small[0x118]).Append(' ').Append(*(long*)(g + 0xD98)).Append(' ').Append(*(long*)(g + 0xDA0));
            else line.Append(" 0 0 0 0 0 0");
            line.Append(" |");
            for (int i = 0; i < s.chain; i++) line.Append(' ').Append(ras[i].ToString("x"));
        }
        else if (t.kind == 2) line.Append("V ").Append(s.at).Append(' ').Append(s.rip.ToString("x"));
        else line.Append("O ").Append(t.tid).Append(' ').Append(s.at).Append(' ').Append(s.rip.ToString("x"));
        line.Append(" |");
        if (s.haveStack)
        {
            int found = 0, most = t.kind == 1 ? 12 : 28;
            for (int i = 0; i + 8 <= s.stackBytes && found < most; i += 8)
            {
                ulong v = *(ulong*)(stack + i);
                if ((v >= exeBase && v < exeEnd) || (v >= dllBase && v < dllEnd)) { line.Append(' ').Append(v.ToString("x")); found++; }
            }
        }
        o.WriteLine(line.ToString());
    }
}
