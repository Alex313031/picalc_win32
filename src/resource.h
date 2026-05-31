// {{NO_DEPENDENCIES}}
// For #define-ing static resources for resource script file(s).
// Used by resource.rc
//
// ID range map (keep new entries inside their range):
//   100-119  IDI_*  Icons
//   120-129  IDR_*  Main program resource (menu / accelerators)
//   130-149  IDD_*  Dialogs
//   200-299  IDM_*  Menu commands
//   300-399  IDC_*  Child control IDs
//   400-499  IDT_*  Timers
//   500-599  IDR_*  Embedded data resources (sounds)

// clang-format off

/* Icons (100-119) */
#define IDI_MAIN                101 /* 32x32 & 48x48 icon */
#define IDI_SMALL               102 /* Small 16x16 icon */
#define IDI_ABOUT               103 /* About Dialog icon */
#define IDI_WINFLAG             104 /* Run Dialog icon */

/* Main application resource - also used to attach menu */
#define IDR_MAIN                120

/* Dialogs (130-149) */
#define IDD_ABOUTDLG            130 // About Dialog
#define IDD_CUSTOM_INPUT        131 // Custom digit/thread count input

/* Menu items (200-299) */
#define IDM_ABOUT               200 // About dialog
#define IDM_EXIT                201 // Exit immediately
#define IDM_CEXIT               202 // Exit confirmation
#define IDM_HELP                203 // Opens help
#define IDM_SAVEAS              204 // Saves client area screenshot
#define IDM_RUN                 205 // "Run" dialog
#define IDM_SLOW                206 // Slow sysmon update speed
#define IDM_MED                 207 // Med speed
#define IDM_FAST                208 // Fast speed
#define IDM_KERNELTIMES         209 // Whether to show kernel times
#define IDM_SOUND               210 // Whether to enable sounds app-wide
#define IDM_CONSOLE             211 // Toggle console window
#define IDM_COLOREDOUTPUT       212 // Whether output pane is b/w or color
#define IDM_CLEARLOG            213 // Clears the log file
#define IDM_OPENLOG             214 // Opens log file with notepad

/* Child control IDs (300-399), grouped by logical role */

/* Bottom output pane + the splitter that separates the two panes */
#define IDC_OUTPUT_EDIT         300 // Output status pane
#define IDC_SPLITTER            301 // Splitter bar
/* Top-pane groupbox frames */
#define IDC_CONTROLS_GROUP      302 // Top-pane controls groupbox
#define IDC_SYSMON_GROUP        303 // System monitor groupbox
#define IDC_CPU_GROUP           304 // CPU metrics sub-groupbox
#define IDC_MEM_GROUP           305 // Memory metrics sub-groupbox
/* Picker rows (label + combobox) */
#define IDC_DIGITS_LABEL        306
#define IDC_DIGITS_COMBO        307
#define IDC_THREADS_LABEL       308
#define IDC_THREADS_COMBO       309
/* Buttons (in display order) */
#define IDC_START_BUTTON        310 // "Calculate!" button
#define IDC_STOP_BUTTON         311
#define IDC_RESULT              312 // "Open Result File" button
#define IDC_CLEARRESULT_BUTTON  313
#define IDC_CLEAROUTPUT_BUTTON  314
#define IDC_CONSOLE_BUTTON      315
#define IDC_ABOUT_BUTTON        316
#define IDC_EXIT_BUTTON         317
/* Custom-input dialog children */
#define IDC_CUSTOM_PROMPT       318 // Static label in custom-input dialog
#define IDC_CUSTOM_EDIT         319 // Edit control in custom-input dialog
/* Sysmon CPU metric value labels */
#define IDC_CPUIDLE             320 // Total CPU % Idle time
#define IDC_CPUUSER             321 // CPU user-mode % usage
#define IDC_CPUKERNEL           322 // Total Kernel CPU % usage
#define IDC_CPUTOTAL            323 // Total CPU % usage
/* Sysmon memory metric value labels */
#define IDC_RAMTOTAL            324 // Amount of RAM used / Total physically installed RAM
#define IDC_PFTOTAL             325 // Amount of page file used / Page file size
#define IDC_VMTOTAL             326 // Commit charge: RAM + Pagefile space
#define IDC_CACHETOTAL          327 // System cache usage / Set system cache size
/* Menu-dispatch pseudo-IDs (re-posted as WM_COMMAND from menu handlers) */
#define IDC_CLEAROUTPUT         328 // Clears output pane
#define IDC_CLEARRESULTS        329 // Clears results .txt file

/* Timers (400-499) */
#define IDT_MONTIMER            400 // For system monitor ticks
#define IDT_WRAPTIMER           401 // For "word wrap" of results window

/* Embedded WAV sounds (500-599) */
#define IDR_TADA_WAV            500 // "Tada" sound when calculation complete
#define IDR_OHNO_WAV            501 // "Oh No!" sound from ImgBurn, when calculation fails or is interrupted
#define IDR_NOTIFY_WAV          502 // Win2K ding sound, used for about dialog notify sound

// For resources to be loaded without an ID from the system.
#ifndef IDC_STATIC
 #define IDC_STATIC             -1
#endif // IDC_STATIC

// clang-format on
