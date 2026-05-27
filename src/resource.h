// {{NO_DEPENDENCIES}}
// For #define-ing static resources for resource script file(s).
// Used by resource.rc

// clang-format off

/* Icons */
#define IDI_MAIN                    101 /* 32x32 & 48x48 icon */
#define IDI_SMALL                   102 /* Small 16x16 icon */
#define IDI_ABOUT                   103 /* About Dialog icon */
#define IDI_WINFLAG                 104 /* Run Dialog icon */

/* Main application resource, also used to attach menu */
#define IDR_MAIN                    120

/* Dialogs */
#define IDD_ABOUTDLG                130 // About Dialog
#define IDD_CUSTOM_INPUT            131 // Custom digit/thread count input

/* Menu items */
#define IDM_ABOUT                   200 // About dialog
#define IDM_EXIT                    201 // Exit immediately
#define IDM_CEXIT                   202 // Exit confirmation
#define IDM_HELP                    203 // Opens help
#define IDM_SAVEAS                  204 // Saves client area screenshot
#define IDM_RUN                     205 // "Run" dialog
#define IDM_SLOW                    206 // Slow sysmon update speed
#define IDM_MED                     207 // Med speed
#define IDM_FAST                    208 // Fast speed

#define IDM_SOUND                   250 // Whether to enale sounds app-wide
#define IDM_CONSOLE                 251 // Toggle console window
#define IDM_COLOREDOUTPUT           252 // Whether output pane is b/w or color
#define IDM_CLEARLOG                253 // Clears the log file
#define IDM_OPENLOG                 254 // Opens log file with notepad

// Embedded background sound WAV files
#define IDR_TADA_WAV                500 // "Tada" sound when calculation complete
#define IDR_OHNO_WAV                501 // "Oh No!" sound from ImgBurn, when calculation fails or is interrupted
#define IDR_NOTIFY_WAV              502 // Win2K ding sound, used for about dialog notify sound

/* Child control IDs */
#define IDC_OUTPUT_EDIT             600 // Output status pane
#define IDC_SPLITTER                601 // Splitter bar
#define IDC_DIGITS_LABEL            602
#define IDC_DIGITS_COMBO            603
#define IDC_THREADS_LABEL           604
#define IDC_THREADS_COMBO           605
#define IDC_START_BUTTON            606
#define IDC_STOP_BUTTON             607
#define IDC_OPENOUT_BUTTON          608
#define IDC_ABOUT_BUTTON            609
#define IDC_CONSOLE_BUTTON          610
#define IDC_EXIT_BUTTON             611
#define IDC_CONTROLS_GROUP          612
#define IDC_CLEARRESULT_BUTTON      613
#define IDC_CLEAROUTPUT_BUTTON      614
#define IDC_CUSTOM_PROMPT           615 // Static label in custom-input dialog
#define IDC_CUSTOM_EDIT             616 // Edit control in custom-input dialog

#define IDC_CPUTOTAL                617 // Total CPU % usage
#define IDC_CPUIDLE                 618 // Total CPU % Idle time
#define IDC_CPUKERNEL               619 // Total Kernel CPU % usage
#define IDC_RAMTOTAL                620 // Amount of RAM used / Total amount physically installed RAM
#define IDC_PFTOTAL                 621 // Amount of page file used / Size of the page file
#define IDC_VMTOTAL                 622 // Commit charge / Virtual Memory size: RAM + Pagefile space
#define IDC_CACHETOTAL              623 // System cache usage / Set system cache size
#define IDC_CLEAROUTPUT             624 // Clears output pane
#define IDC_CLEARRESULTS            625 // Clears results .txt file

// For resources to be loaded without an ID from the system.
#ifndef IDC_STATIC
 #define IDC_STATIC                 -1
#endif // IDC_STATIC
// clang-format on
