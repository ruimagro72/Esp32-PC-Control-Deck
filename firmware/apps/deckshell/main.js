// Entry in KryonOS's launcher: switch the main screen back to the Deck shell. The kernel runs
// /sd/shell.js as soon as this app ends (main.cpp, s_shellOn). The shell hides this entry.
System.setShell("/sd/shell.js");
