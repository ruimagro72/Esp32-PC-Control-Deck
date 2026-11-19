// Entry in KryonOS's launcher: switch the main screen back to the Deck shell. The kernel runs
// /sd/system/SystemApps/shell.js as soon as this app ends (main.cpp, s_shellOn). The shell hides this entry.
System.setShell("/sd/system/SystemApps/shell.js");
