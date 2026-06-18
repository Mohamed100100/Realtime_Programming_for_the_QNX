
# Running and Debugging in QNX Momentics IDE

## Overview

This module covers how to execute and debug QNX applications using the Momentics IDE. There are two primary approaches to running your program: manual deployment to the target followed by command-line execution, or using the IDE's launch configurations for automated remote execution. For debugging, launch configurations are essential — they tell the IDE how to start your program, connect the debugger, and present runtime information. Once created, launch configurations are saved and reusable, streamlining repeated development cycles.

---

## Two Ways to Run Your Program

| Method | Description | Best For |
|--------|-------------|----------|
| **Manual deployment** | Copy executable to target via File System Navigator, run from target shell | Quick tests, scripts, when IDE is not needed |
| **Launch configuration** | IDE handles build, deploy, execute automatically | Regular development, debugging, repeated runs |

Both methods ultimately run the same compiled binary on the QNX target. The difference is who manages the transfer and execution — you manually, or the IDE automatically.

---

## Manual Deployment and Execution

This approach gives you direct control over where and how your program runs on the target.

**Steps:**
1. Build your project successfully (no errors)
2. Locate the executable in the Project Explorer under the Binaries folder
3. Use the **Target File System Navigator** to browse the target's directory structure
4. Drag and drop (or copy) your executable from the host to the desired target directory
5. Open a terminal on the target (SSH, serial console, or IDE terminal)
6. Navigate to the directory where you placed the executable
7. Run the program with any needed arguments: `./my_program arg1 arg2`

This method is useful when you want to run programs without IDE overhead, test in production-like conditions, or execute utilities and scripts that don't need debugging.

---

## Launch Configurations

Launch configurations are the IDE's way of automating the build-deploy-run cycle. They encapsulate all settings needed to execute your program on a specific target. Once configured, you can run or debug repeatedly with a single click or keyboard shortcut.

### Creating a Run Configuration

Right-click on your project or executable in the Project Explorer, then select **Run As**. Two options appear:

| Option | Behavior |
|--------|----------|
| **First option (quick run)** | Immediately runs the program using default settings without opening configuration dialog |
| **Second option (configure and run)** | Opens the launch configuration dialog for customization before running |

The quick run option is convenient when your program needs no special arguments or environment settings. The IDE uses sensible defaults: the selected executable, the default target connection, and no command-line arguments.

The configure-and-run option opens the **Run Configurations** dialog, where you can customize every aspect of execution.

### Run Configuration Dialog

The configuration dialog contains several tabs for different settings:

| Tab | Purpose | Example Settings |
|-----|---------|----------------|
| **Main** | Select project, executable, and target | Project name, binary path, target IP |
| **Arguments** | Pass command-line arguments to your program | `"world"`, `-v --config=/etc/myapp.cfg` |
| **Environment** | Set environment variables | `LD_LIBRARY_PATH=/lib:/usr/lib` |
| **Debugger** | Debug-specific settings (only in Debug configurations) | GDB options, source mapping |

**The Arguments tab** is particularly important for programs that require input parameters. Instead of hardcoding values or using interactive prompts, you can pass arguments exactly as you would on the command line.

```
┌─────────────────────────────────────────────────────────────────────┐
│              RUN CONFIGURATION DIALOG                                  │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Run Configurations                                            │   │
│  │                                                             │   │
│  │  Name: [hello_run________________________________________]   │   │
│  │                                                             │   │
│  │  [Main] [Arguments] [Environment] [Debugger] [Source] [Common] │   │
│  │                                                             │   │
│  │  ── Main Tab ─────────────────────────────────────────────   │   │
│  │  Project: [hello________________________________________]   │   │
│  │  C/C++ Application: [hello________________________________] │   │
│  │  Target: [192.168.56.103________________________________]   │   │
│  │                                                             │   │
│  │  ── Arguments Tab ─────────────────────────────────────────   │   │
│  │  Program arguments:                                           │   │
│  │  ┌─────────────────────────────────────────────────────┐   │   │
│  │  │ world                                               │   │   │
│  │  │ --verbose                                           │   │   │
│  │  │ --config=/etc/hello.conf                            │   │   │
│  │  └─────────────────────────────────────────────────────┘   │   │
│  │                                                             │   │
│  │  Working directory: [Default: remote executable's dir____]   │   │
│  │                                                             │   │
│  │              [Run]  [Cancel]  [Apply]                         │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  Arguments are passed to main(int argc, char *argv[]) exactly as    │
│  they would be on the command line: argc=4, argv[0]="hello",        │
│  argv[1]="world", argv[2]="--verbose", argv[3]="--config=/etc/..." │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Saving vs. Not Saving Configurations

When you run with the quick option (first option), the IDE may offer to save the configuration. You can:
- **Save** — Store the configuration for future reuse; appears in Run history
- **Don't save** — One-time run; no persistent configuration created

For regular development, saving configurations is recommended. Named configurations like `hello_run`, `hello_debug`, `hello_test_args` let you switch between different execution scenarios instantly.

---

## Debug Configurations

Debugging uses the same launch configuration framework but with additional settings for the GDB debugger and debug-specific views.

### Creating a Debug Configuration

Right-click your project or executable → **Debug As** → select the configuration option. This opens the **Debug Configurations** dialog, similar to Run Configurations but with debugger-specific tabs.

### The Debug Perspective

When you start a debug session, the IDE automatically switches to the **Debug perspective**, which rearranges views for debugging tasks.

```
┌─────────────────────────────────────────────────────────────────────┐
│              DEBUG PERSPECTIVE LAYOUT                                  │
│                                                                      │
│  ┌─────────────────┬─────────────────────────┬──────────────────┐   │
│  │                 │                         │                  │   │
│  │  DEBUG VIEW     │    EDITOR               │   VARIABLES      │   │
│  │  (call stack)   │    (source code with    │   VIEW           │   │
│  │                 │     breakpoint markers) │                  │   │
│  │  ┌───────────┐  │                         │  ┌───────────┐   │   │
│  │  │ hello     │  │  40:  int main() {     │  │ Name      │   │   │
│  │  │  main()   │  │  41:      init();      │  │ ───────── │   │   │
│  │  │  [0x0804] │  │  42:      ● printf(...); │  │ argc = 2  │   │   │
│  │  │  thread1  │  │  43:      process();   │  │ argv[0]   │   │   │
│  │  │  [0x0808] │  │  44:      return 0;    │  │   = "hello"│   │   │
│  │  └───────────┘  │  45:  }                 │  │ argv[1]   │   │   │
│  │                 │                         │  │   = "world"│   │   │
│  │                 │  ● = breakpoint         │  │ status = 0│   │   │
│  │                 │  ► = current line       │  │ buffer =  │   │   │
│  │                 │                         │  │   {0, 0,..}│   │   │
│  └─────────────────┴─────────────────────────┴──────────────────┘   │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  CONSOLE / BREAKPOINTS / EXPRESSIONS / MEMORY (bottom panel)  │   │
│  │                                                             │   │
│  │  [Console] [Breakpoints] [Expressions] [Memory]             │   │
│  │                                                             │   │
│  │  Console: Program output appears here                         │   │
│  │  Breakpoints: List of all set breakpoints with enable/disable│   │
│  │  Expressions: Watch custom expressions (e.g., buffer[0] + 5) │   │
│  │  Memory: Raw hex dump of memory at any address                │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Debug Controls

The Debug view provides buttons for controlling program execution:

| Button | Action | Shortcut | Description |
|--------|--------|----------|-------------|
| **Resume** | Continue execution | F8 | Run until next breakpoint or program end |
| **Suspend** | Pause execution | — | Halt program at current location |
| **Terminate** | Stop program | Ctrl+F2 | End debug session |
| **Step Into** | Enter function call | F5 | Follow execution into called function |
| **Step Over** | Execute line, stay in current function | F6 | Execute function call without entering it |
| **Step Return** | Run until current function returns | F7 | Complete current function, return to caller |

### Setting Breakpoints

Breakpoints pause execution at specific lines, allowing you to inspect program state.

To set a breakpoint:
1. Open the source file in the editor
2. Double-click in the left margin next to the desired line (or right-click → Toggle Breakpoint)
3. A blue dot appears in the margin indicating the breakpoint is active
4. Run in debug mode — execution pauses when the line is reached

Breakpoint types:
- **Line breakpoint** — Most common; stops at a specific source line
- **Conditional breakpoint** — Stops only when a specified expression evaluates to true
- **Watchpoint** — Stops when a variable is read or written

### Inspecting Variables

When execution is paused at a breakpoint, the **Variables view** shows all variables visible in the current scope:

- Local variables in the current function
- Function parameters
- Global variables (if configured to show)
- Structure and array contents (expandable)

You can:
- **Examine values** — See current state of variables
- **Modify values** — Change variable contents to test different scenarios
- **Add expressions** — Watch computed expressions like `buffer[0] + offset`

### The Expressions View

For monitoring specific values that aren't simple variables, the **Expressions** view lets you define custom watch expressions. These can be:
- Array elements: `sensor_data[5]`
- Structure fields: `config.timeout_ms`
- Computed values: `temperature * 1.8 + 32`
- Function return values: `strlen(buffer)`

Expressions update each time execution pauses, showing the current computed value.

### Memory Inspection

The **Memory** view provides a raw hexadecimal dump of memory at any address. This is useful for:
- Examining hardware-mapped memory regions
- Verifying buffer contents at the byte level
- Debugging memory corruption issues
- Inspecting shared memory segments

You can enter any valid memory address (or expression evaluating to an address) and view the contents in various formats: hexadecimal, ASCII, decimal, or floating-point.

---

## Reusing Launch Configurations

Once created, launch configurations persist across IDE sessions. They appear in:
- The **Run** or **Debug** toolbar dropdown menus
- The **Run Configurations** and **Debug Configurations** dialogs
- Keyboard shortcut history (F11 for debug, Ctrl+F11 for run)

This reusability means you configure once, then run or debug repeatedly with minimal effort. Common patterns include:
- One run configuration with no arguments (quick test)
- One run configuration with test data arguments
- One debug configuration with breakpoints set in critical functions
- One debug configuration with verbose logging enabled

---

## Summary

| Task | How to Access | Key Feature |
|------|-------------|-------------|
| Quick run | Right-click → Run As → first option | Immediate execution, no dialog |
| Configure run | Right-click → Run As → second option | Arguments, environment, target selection |
| Quick debug | Right-click → Debug As → first option | Immediate debug session |
| Configure debug | Right-click → Debug As → second option | Debugger settings, source mapping |
| Set breakpoint | Double-click editor left margin | Pause execution at line |
| Step through code | F5 (into), F6 (over), F7 (return) | Controlled execution flow |
| Inspect variables | Variables view during pause | See and modify program state |
| Watch expressions | Expressions view | Monitor custom computed values |
| Inspect memory | Memory view | Raw hex dump at any address |

The launch configuration system is the heart of IDE-based development. It bridges the gap between your host workstation and the remote QNX target, automating the complexities of cross-platform execution while providing powerful debugging capabilities for diagnosing runtime behavior.

---
*Happy debugging!* 🚀
