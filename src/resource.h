// {{NO_DEPENDENCIES}}
// For #define-ing static resources for resource script file(s).
// Used by resource.rc

// clang-format off

/* Icons */
#define IDI_MAIN                    101 /* 32x32 & 48x48 icon */
#define IDI_SMALL                   102 /* Small 16x16 icon */
#define IDI_ABOUT                   103 /* About Dialog icon */

/* Main application resource, also used to attach menu */
#define IDR_MAIN                    120

/* Dialogs */
#define IDD_ABOUTDLG                130 // About Dialog

/* Menu items */
#define IDM_ABOUT                   200
#define IDM_EXIT                    201
#define IDM_CEXIT                   202
#define IDM_HELP                    203
#define IDM_SAVEAS                  204
#define IDM_CLEAROUTPUT             205

#define IDM_SOUND                   250
#define IDM_CONSOLE                 251
#define IDM_CLEARRESULTS            252
#define IDM_CLEARLOG                253

// Embedded background sound WAV files
#define IDR_TADA_WAV                500
#define IDR_OHNO_WAV                501
#define IDR_NOTIFY_WAV              502

/* Child control IDs */
#define IDC_OUTPUT_EDIT             600
#define IDC_SPLITTER                601
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

// For resources to be loaded without an ID from the system.
#ifndef IDC_STATIC
 #define IDC_STATIC                 -1
#endif // IDC_STATIC
// clang-format on
