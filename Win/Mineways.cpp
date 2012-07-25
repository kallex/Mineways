/*
Copyright (c) 2011, Sean Kasun
All rights reserved.
Modified by Eric Haines, copyright (c) 2011.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "stdafx.h"
#include "Mineways.h"
#include "ColorSchemes.h"
#include "ExportPrint.h"
#include "zip.h"
#include <assert.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <CommDlg.h>
#include <stdio.h>
#include <math.h>

// zoomed all the way in. We could allow this to be larger...
// It's useful to have it high for Nether <--> overworld switches
#define MAXZOOM 40.0
// zoomed all the way out
#define MINZOOM 1.0

// how far outside the rectangle we'll select the corners and edges of the selection rectangle
#define SELECT_MARGIN 5

#define MAX_LOADSTRING 100

// window margins - there should be a lot more defines here...
#define MAIN_WINDOW_TOP (30+30)
#define SLIDER_LEFT	90


// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name
TCHAR *worlds[1000];							// up to 1000 worlds

static Options gOptions = {0,   // which world is visible
    BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTOP | BLF_FLATSIDE,   // what's exportable (really, set on output)
    0x0,
    0,  // start with low memory
    NULL};

static wchar_t gWorld[MAX_PATH];							//path to currently loaded world
static BOOL gSameWorld=FALSE;
static wchar_t gSelectTerrain[MAX_PATH];					//path to selected terrain.png file, if any
static BOOL gLoaded=FALSE;								//world loaded?
static double gCurX,gCurZ;								//current X and Z
static int gLockMouseX=0;                               // if true, don't allow this coordinate to change with mouse, 
static int gLockMouseZ=0;
static double gCurScale=MINZOOM;					    //current scale
static int gCurDepth=MAP_MAX_HEIGHT;					//current depth
static int gStartHiX,gStartHiZ;						    //starting highlight X and Z

static BOOL gHighlightOn=FALSE;

static int gSpawnX,gSpawnY,gSpawnZ;
static int gPlayerX,gPlayerY,gPlayerZ;

// minimum depth output by default, sea level including water (or not, in 1.9 - TODO: we could key off of version number in world)
// note: 51 this gets you to bedrock in deep lakes
#define MIN_OVERWORLD_DEPTH SEA_LEVEL

static int gTargetDepth=MIN_OVERWORLD_DEPTH;								//how far down the depth is stored, for export

// Export 3d print and view data
static ExportFileData gExportPrintData;
static ExportFileData gExportViewData;
// this one is set to whichever is active;
static ExportFileData *gpEFD;

static int gOverworldHideStatus=0x0;

static wchar_t gCurrentDirectory[MAX_PATH];

// low, inside, high for selection area, fourth value is minimum height found below selection box
static int gHitsFound[4];
static int gFullLow=1;
static int gAdjustingSelection=0;
static int gShowPrintStats=1;
static int gAutocorrectDepth=1;

static int gBottomControlEnabled = FALSE;

static BOOL gPrintModel = FALSE;
static BOOL gExported=0;
static int gFileType = 0;
static TCHAR gExportPath[MAX_PATH] = _T("");


// Error codes
static struct {
    TCHAR *text;
    TCHAR *caption;
    unsigned int type;
} gPopupInfo[]={
    {_T("No error"), _T("No error"), MB_OK},	//00
    {_T("Thin walls possible.\nThe thickness of a single block is smaller\nthan the recommended wall thickness.\nPrint only if you know what you're doing."), _T("Informational"), MB_OK|MB_ICONINFORMATION},	// <<0
    {_T("Sum of dimensions low.\nThe sum of the dimensions of the model is less than 65 mm.\nThis is officially too small for a Shapeways color sandstone\nmodel, but they will probably print it anyway."), _T("Informational"), MB_OK|MB_ICONINFORMATION},	// <<1
    {_T("Too many polygons.\nThere are more than one million polygons in file.\nThis is usually too many for Shapeways."), _T("Informational"), MB_OK|MB_ICONINFORMATION},	// <<2
    {_T("Multiple separate parts found after processing.\nThis may not be what you want to print. Increase the\nvalue for 'Delete floating parts' to delete these. Try\nthe 'Debug: show separate parts' export option to\nsee if the model is what you expected."), _T("Informational"), MB_OK|MB_ICONINFORMATION},	// <<3
	{_T("At least one dimension of the model is too long.\nCheck the dimensions for this printer's material:\nlook at the top of the model file."), _T("Informational"), MB_OK|MB_ICONINFORMATION},	// <<4
    {_T("Warning: Mineways encountered an unknown block type.\nSuch blocks are converted to stone.\n\nDownload a new version of Mineways from mineways.com."), _T("Informational"), MB_OK|MB_ICONINFORMATION},	// <<5
    {_T("No solid blocks found;\nno file output"), _T("Export warning"), MB_OK|MB_ICONWARNING},	//06
    {_T("All solid blocks were deleted;\nno file output"), _T("Export warning"), MB_OK|MB_ICONWARNING},	//07
    {_T("Error: no terrain.png file found.\nPlease put a terrain.png file in the same\ndirectory as mineways.exe. If this doesn't work,\nplease use the 'File|Select terrain.png for export'\nmenu item and pick the terrain.png file."), _T("Export error"), MB_OK|MB_ICONERROR},	//08
    {_T("Error creating export file;\nno file output"), _T("Export error"), MB_OK|MB_ICONERROR},	//09
    {_T("Error writing to export file;\npartial file output"), _T("Export error"), MB_OK|MB_ICONERROR},	//10
    {_T("Error: the incoming terrain.png file\nresolution must be a power of two\n(like 256x256), square, and at least 16x16."), _T("Export error"), MB_OK|MB_ICONERROR},	//11
};


// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	About(HWND, UINT, WPARAM, LPARAM);
static int loadWorld();
static void worldPath(TCHAR *path);
static void enableBottomControl( int state, HWND hwndBottomSlider, HWND hwndBottomLabel, HWND hwndInfoBottomLabel );
static void validateItems(HMENU menu);
static int loadWorldList(HMENU menu);
static void draw();
static void updateStatus(int mx, int mz, int my, const char *blockLabel, HWND hwndStatus);
static void populateColorSchemes(HMENU menu);
static void useCustomColor(int wmId,HWND hWnd);
static int findColorScheme(wchar_t* name);
static void setSlider( HWND hWnd, HWND hwndSlider, HWND hwndLabel, int depth );
static void syncCurrentHighlightDepth();
static int saveObjFile( HWND hWnd, wchar_t *objFileName, BOOL printModel, int fileType, wchar_t *terrainFileName, BOOL showDialog );
static const wchar_t *removePath( const wchar_t *src );
static void initializeExportDialogData();

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
    GetCurrentDirectory(MAX_PATH,gCurrentDirectory);
    gSelectTerrain[0] = (wchar_t)0;

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    initializeExportDialogData();

    MSG msg;
    HACCEL hAccelTable;

    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDC_MINEWAYS, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MINEWAYS));

    // Main message loop:
    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//  COMMENTS:
//
//    This function and its usage are only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style			= CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc	= WndProc;
    wcex.cbClsExtra		= 0;
    wcex.cbWndExtra		= 0;
    wcex.hInstance		= hInstance;
    wcex.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MINEWAYS));
    wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName	= MAKEINTRESOURCE(IDC_MINEWAYS);
    wcex.lpszClassName	= szWindowClass;
    wcex.hIconSm		= LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassEx(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   HWND hWnd;

   hInst = hInstance; // Store instance handle in our global variable

   hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, 480, 582, NULL, NULL, hInstance, NULL);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND	- process the application menu
//  WM_PAINT	- Paint the main window
//  WM_DESTROY	- post a quit message and return
//
//
static unsigned char *map;
static int bitWidth=0;
static int bitHeight=0;
static HWND progressBar=NULL;
static HBRUSH ctlBrush=NULL;
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId; // set but not used, wmEvent;
    PAINTSTRUCT ps;
    HDC hdc;
    static HWND hwndSlider,hwndBottomSlider,hwndLabel,hwndBottomLabel,hwndInfoLabel,hwndInfoBottomLabel,hwndStatus;
    static BITMAPINFO bmi;
    static HBITMAP bitmap=NULL;
    static HDC hdcMem=NULL;
    static int oldX=0,oldY=0;
    static const char *blockLabel="";
    static BOOL dragging=FALSE;
    static BOOL hdragging=FALSE;
    static int moving=0;
    INITCOMMONCONTROLSEX ice;
    DWORD pos;
    wchar_t text[4];
    RECT rect;
    TCHAR path[MAX_PATH];
    OPENFILENAME ofn;
    int mx,my,mz,type;
    static LPARAM holdlParam;
    int on, minx, miny, minz, maxx, maxy, maxz;
	BOOL saveOK;

    // Show message
#ifdef _DEBUG
    wchar_t buf[100];
    swprintf( buf, 100, L"Message: %d\n", message);
    OutputDebugStringW( buf );
#endif

    switch (message)
    {
    case WM_CREATE:

        validateItems(GetMenu(hWnd));
        if ( loadWorldList(GetMenu(hWnd)) )
		{
			MessageBox( NULL, _T("Warning:\nAt least one of your worlds has not been converted to the Anvil format.\nThese worlds will be shown as disabled in the Open World menu.\nTo convert a world, run Minecraft 1.2 or later and play it, then quit.\nTo use Mineways on an old-style McRegion world, download\nVersion 1.15 from the mineways.com site."),
				_T("Warning"), MB_OK|MB_ICONWARNING);
		}
        populateColorSchemes(GetMenu(hWnd));
        CheckMenuItem(GetMenu(hWnd),IDM_CUSTOMCOLOR,MF_CHECKED);

        ctlBrush=CreateSolidBrush(GetSysColor(COLOR_WINDOW));

        ice.dwSize=sizeof(INITCOMMONCONTROLSEX);
        ice.dwICC=ICC_BAR_CLASSES;
        InitCommonControlsEx(&ice);
        GetClientRect(hWnd,&rect);
		hwndSlider=CreateWindowEx(
			0,TRACKBAR_CLASS,L"Trackbar Control",
			WS_CHILD | WS_VISIBLE | TBS_NOTICKS,
			SLIDER_LEFT,0,rect.right-rect.left-40-SLIDER_LEFT,30,
			hWnd,(HMENU)ID_LAYERSLIDER,NULL,NULL);
		SendMessage(hwndSlider,TBM_SETRANGE,TRUE,MAKELONG(0,MAP_MAX_HEIGHT));
		SendMessage(hwndSlider,TBM_SETPAGESIZE,0,10);
		EnableWindow(hwndSlider,FALSE);

		hwndLabel=CreateWindowEx(
			0,L"STATIC",NULL,
			WS_CHILD | WS_VISIBLE | ES_RIGHT,
			rect.right-40,5,30,20,
			hWnd,(HMENU)ID_LAYERLABEL,NULL,NULL);
		SetWindowText(hwndLabel,MAP_MAX_HEIGHT_STRING);
		EnableWindow(hwndLabel,FALSE);

		hwndBottomSlider=CreateWindowEx(
			0,TRACKBAR_CLASS,L"Trackbar Control",
			WS_CHILD | WS_VISIBLE | TBS_NOTICKS,
			SLIDER_LEFT,30,rect.right-rect.left-40-SLIDER_LEFT,30,
			hWnd,(HMENU)ID_LAYERBOTTOMSLIDER,NULL,NULL);
		SendMessage(hwndBottomSlider,TBM_SETRANGE,TRUE,MAKELONG(0,MAP_MAX_HEIGHT));
		SendMessage(hwndBottomSlider,TBM_SETPAGESIZE,0,10);
		EnableWindow(hwndBottomSlider,FALSE);

		hwndBottomLabel=CreateWindowEx(
			0,L"STATIC",NULL,
			WS_CHILD | WS_VISIBLE | ES_RIGHT,
			rect.right-40,35,30,20,
			hWnd,(HMENU)ID_LAYERBOTTOMLABEL,NULL,NULL);
		SetWindowText(hwndBottomLabel,SEA_LEVEL_STRING);
		EnableWindow(hwndBottomLabel,FALSE);

		setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth );

		// label to left
		hwndInfoLabel=CreateWindowEx(
			0,L"STATIC",NULL,
			WS_CHILD | WS_VISIBLE | ES_LEFT,
			5,5,SLIDER_LEFT,20,
			hWnd,(HMENU)ID_LAYERINFOLABEL,NULL,NULL);
		SetWindowText(hwndInfoLabel,L"Max height");
		EnableWindow(hwndInfoLabel,FALSE);

		hwndInfoBottomLabel=CreateWindowEx(
			0,L"STATIC",NULL,
			WS_CHILD | WS_VISIBLE | ES_LEFT,
			5,35,SLIDER_LEFT,20,
			hWnd,(HMENU)ID_LAYERINFOBOTTOMLABEL,NULL,NULL);
		SetWindowText(hwndInfoBottomLabel,L"Lower depth");
		EnableWindow(hwndInfoBottomLabel,FALSE);

        hwndStatus=CreateWindowEx(
            0,STATUSCLASSNAME,NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER,
            -100,-100,10,10,
            hWnd,(HMENU)ID_STATUSBAR,NULL,NULL);
        {
        int parts[]={300,400};
        RECT rect;
        SendMessage(hwndStatus,SB_SETPARTS,2,(LPARAM)parts);

        progressBar=CreateWindowEx(
            0,PROGRESS_CLASS,NULL,
            WS_CHILD | WS_VISIBLE,
            0,0,10,10,hwndStatus,(HMENU)ID_PROGRESS,NULL,NULL);
        SendMessage(hwndStatus,SB_GETRECT,1,(LPARAM)&rect);
        MoveWindow(progressBar,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,TRUE);
        SendMessage(progressBar,PBM_SETSTEP,(WPARAM)5,0);
        SendMessage(progressBar,PBM_SETPOS,0,0);
        }

        rect.top+=MAIN_WINDOW_TOP;	// add in two sliders, 30 each
        bitWidth=rect.right-rect.left;
        bitHeight=rect.bottom-rect.top;
        ZeroMemory(&bmi.bmiHeader,sizeof(BITMAPINFOHEADER));
        bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth=bitWidth;
        bmi.bmiHeader.biHeight=-bitHeight; //flip
        bmi.bmiHeader.biPlanes=1;
        bmi.bmiHeader.biBitCount=32;
        bmi.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(NULL,&bmi,DIB_RGB_COLORS,(void **)&map,NULL,0);
        break;
    case WM_LBUTTONDOWN:
        dragging=TRUE;
        hdragging=FALSE;// just in case
        SetFocus(hWnd);
        SetCapture(hWnd);
        oldX=LOWORD(lParam);
        oldY=HIWORD(lParam);
        break;
    case WM_RBUTTONDOWN:
        gAdjustingSelection = 0;
        if (gLoaded)
        {
            int wasDragging = hdragging;
            hdragging=TRUE;
            dragging=FALSE;// just in case
            SetFocus(hWnd);
            SetCapture(hWnd);
            
            // get mouse position in world space
            (void)IDBlock(LOWORD(lParam),HIWORD(lParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
                bitWidth,bitHeight,gCurScale,&mx,&my,&mz,&type);
            holdlParam=lParam;

            gStartHiX=mx;
            gStartHiZ=mz;
            gHighlightOn=TRUE;

            // these track whether a selection height volume has blocks in it,
            // low, medium, high, and minimum low-height found
            gHitsFound[0] = gHitsFound[1] = gHitsFound[2] = 0;
            gHitsFound[3] = MAP_MAX_HEIGHT+1;

            // now to check the corners: is this location near any of them?
            GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );

            // if we weren't dragging before (making a selection), and there is
            // an active selection, see if we select on the selection border
            if ( !wasDragging && on )
            {
                // highlighting is on, check the corners: inside bounds of current selection?
                if ( ( mx >= minx - SELECT_MARGIN/gCurScale ) && 
                    ( mx <= maxx + SELECT_MARGIN/gCurScale ) &&
                    ( mz >= minz - SELECT_MARGIN/gCurScale ) &&
                    ( mz <= maxz + SELECT_MARGIN/gCurScale ) )
                {
                    int startx,endx,startz,endz;
                    int innerx = (int)(SELECT_MARGIN/gCurScale);
                    int innerz = (int)(SELECT_MARGIN/gCurScale);
                    gAdjustingSelection = 1;

                    if ( innerx > maxx-minx )
                    {
                        innerx = (maxx-minx-1)/2;
                    }
                    if ( innerz > maxz-minz )
                    {
                        innerz = (maxz-minz-1)/2;
                    }

                    if ( mx <= minx + innerx )
                    {
                        // in minx zone
                        startx = maxx;
                        endx = mx;
                    }
                    else if ( mx >= maxx - innerx )
                    {
                        // in maxx zone
                        startx = minx;
                        endx = mx;
                    }
                    else
                    {
                        // in middle: lock x
                        gLockMouseX = 1;
                        startx = minx;
                        endx = maxx;
                    }

                    if ( mz <= minz + innerz )
                    {
                        // in minx zone
                        startz = maxz;
                        endz = mz;
                    }
                    else if ( mz >= maxz - innerz )
                    {
                        // in maxz zone
                        startz = minz;
                        endz = mz;
                    }
                    else
                    {
                        // in middle: lock z
                        gLockMouseZ = 1;
                        startz = minz;
                        endz = maxz;
                    }

                    // if in center zone, then it's just a regular mouse down
                    if ( gLockMouseX && gLockMouseZ )
                    {
                        gLockMouseX = gLockMouseZ = 0;
                        gAdjustingSelection = 0;
                    }
                    else
                    {
                        // stick the rectangle to the mouse
                        gStartHiX = startx;
                        mx = endx;
                        gStartHiZ = startz;
                        mz = endz;
                    }
#ifdef _DEBUG
                    wchar_t bufa[100];
                    swprintf( bufa, 100, L"startx %d, endx %d, startz %d, endz %d\n", startx, endx, startz, endz );
                    OutputDebugStringW( bufa );
#endif
                }
            }

            SetHighlightState(gHighlightOn,gStartHiX,gTargetDepth,gStartHiZ,mx,gCurDepth,mz);
			enableBottomControl( gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
            validateItems(GetMenu(hWnd));
            draw();
            InvalidateRect(hWnd,NULL,FALSE);
            UpdateWindow(hWnd);
        }
        break;
    case WM_MOUSEWHEEL:
        if (gLoaded)
        {
            int zDelta=GET_WHEEL_DELTA_WPARAM(wParam);
            // ratchet zoom up by 2x when zoom of 8 or higher is reached, so it zooms faster
            gCurScale+=((double)zDelta/WHEEL_DELTA)*(pow(gCurScale,1.2)/gCurScale);
            gCurScale = clamp(gCurScale,MINZOOM,MAXZOOM);
            draw();
            InvalidateRect(hWnd,NULL,FALSE);
            UpdateWindow(hWnd);
        }
        break;
    case WM_LBUTTONUP:
        dragging=FALSE;
        hdragging=FALSE;	// just in case
        ReleaseCapture();
        break;
    case WM_MBUTTONDOWN:
        // set new target depth
        hdragging=FALSE;
        dragging=FALSE;		// just in case
        gLockMouseX = gLockMouseZ = 0;
        int mx,mz;
        blockLabel=IDBlock(LOWORD(lParam),HIWORD(lParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
            bitWidth,bitHeight,gCurScale,&mx,&my,&mz,&type);
        holdlParam=lParam;
        if ( my >= 0 && my <= MAP_MAX_HEIGHT )
        {
            // special test: if type is a flattop, then select the location one lower for export
            if ( (gBlockDefinitions[type].flags&BLF_FLATTOP) && (my>0) )
            {
                my--;
            }
            gTargetDepth = my;
            gTargetDepth = clamp(gTargetDepth,0,MAP_MAX_HEIGHT);   // should never happen that a flattop is at 0, but just in case
            // also set highlight state to new depths
			setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth );
            GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
            SetHighlightState(on,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
			enableBottomControl( on, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );

            updateStatus(mx,mz,my,blockLabel,hwndStatus);

            validateItems(GetMenu(hWnd));
            draw();
            InvalidateRect(hWnd,NULL,FALSE);
            UpdateWindow(hWnd);
        }
        break;
    case WM_RBUTTONUP:
        hdragging=FALSE;
        dragging=FALSE;		// just in case
        gLockMouseX = gLockMouseZ = 0;
        ReleaseCapture();
        
        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
        if ( minx == maxx && minz == maxz )
        {
            // if mouse up in same place as mouse down, turn selection off - who exports one cube column?
            gHighlightOn=FALSE;
            SetHighlightState(gHighlightOn,0,0,0,0,0,0);
			enableBottomControl( gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );

            int mx,mz;
            blockLabel=IDBlock(LOWORD(lParam),HIWORD(lParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
                bitWidth,bitHeight,gCurScale,&mx,&my,&mz,&type);
            holdlParam=lParam;
            if ( my >= 0 && my <= MAP_MAX_HEIGHT )
            {
                // special test: if type is a flattop, then select the location one lower for export
                if ( (gBlockDefinitions[type].flags&BLF_FLATTOP) && (my>0) )
                {
                    my--;
                }
				// Could set the target depth to whatever depth we're over. Helps Mac users without a middle mouse button.
                //gTargetDepth = my;
                updateStatus(mx,mz,my,blockLabel,hwndStatus);
            }

            validateItems(GetMenu(hWnd));
            draw();
            InvalidateRect(hWnd,NULL,FALSE);
            UpdateWindow(hWnd);
        }
        else
        {
            // Area selected.
            // Check if this selection is not an adjustment
            if ( !gAdjustingSelection )
            {
                // Not an adjustment, but a new selection. As such, test if there's something below to be selected and
                // there's nothing in the actual selection volume.
                if ( gHitsFound[0] && !gHitsFound[1] )
                {
                    // make sure there's some lower depth to use to replace current target depth
                    if ( gHitsFound[3] < gTargetDepth )
                    {
                        wchar_t msgString[1024];
                        // send warning, set to min height found, then redo!
                        if ( gFullLow )
                        {
                            gFullLow = 0;
                            swprintf_s(msgString,1024,L"All blocks in your selection are below the current lower depth of %d.\n\nWhen you select, you're selecting in three dimensions, and there\nis a lower depth, displayed in the status bar at the bottom.\nYou can adjust this depth by using the lower slider or '[' & ']' keys.\n\nThe depth will be reset to %d to include all visible blocks.",
                                gTargetDepth, gHitsFound[3] );
                        }
                        else
                        {
                            swprintf_s(msgString,1024,L"All blocks in your selection are below the current lower depth of %d.\n\nThe depth will be reset to %d to include all visible blocks.",
                                gTargetDepth, gHitsFound[3] );
                        }
                        MessageBox( NULL, msgString,
                            _T("Informational"), MB_OK|MB_ICONINFORMATION);
                        gTargetDepth = gHitsFound[3];
						setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth );
                        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
                        // update target depth
                        SetHighlightState(gHighlightOn,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
						enableBottomControl( gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
                        draw();
                        InvalidateRect(hWnd,NULL,FALSE);
                        UpdateWindow(hWnd);
                    }
                    else
                    {
                        // funny: target depth is lower than min height in lower section, which
                        // means lower section not really set. Don't do any warning, I guess.
                    }
                }
                // else, test if there's something in both volumes and offer to adjust.
                else if ( gAutocorrectDepth &&
					      ((  gHitsFound[0] && gHitsFound[1] && ( gHitsFound[3] < gTargetDepth ) ) ||
					       ( !gHitsFound[0] && gHitsFound[1] && ( gHitsFound[3] > gTargetDepth) )) )
                {
                    // send warning
                    int retval;
                    wchar_t msgString[1024];
                    // send warning, set to min height found, then redo!
					if ( gHitsFound[3] < gTargetDepth )
					{
						if ( gFullLow )
						{
							gFullLow = 0;
							swprintf_s(msgString,1024,L"Some blocks in your selection are visible below the current lower depth of %d.\n\nWhen you select, you're selecting in three dimensions, and there\nis a lower depth, shown on the second slider at the top.\nYou can adjust this depth by using this slider or '[' & ']' keys.\n\nDo you want to set the depth to %d to select all visible blocks?\nSelect 'Cancel' to turn off this autocorrection system.",
								gTargetDepth, gHitsFound[3] );
						}
						else
						{
							swprintf_s(msgString,1024,L"Some blocks in your selection are visible below the current lower depth of %d.\n\nDo you want to set the depth to %d to select all visible blocks?\nSelect 'Cancel' to turn off this autocorrection system.",
								gTargetDepth, gHitsFound[3] );
						}
					}
					else
					{
						if ( gFullLow )
						{
							gFullLow = 0;
							swprintf_s(msgString,1024,L"The current selection lower depth of %d contains hidden lower layers.\n\nWhen you select, you're selecting in three dimensions, and there\nis a lower depth, shown on the second slider at the top.\nYou can adjust this depth by using this slider or '[' & ']' keys.\n\nDo you want to set the depth to %d to minimize the underground?\nSelect 'Cancel' to turn off this autocorrection system.",
								gTargetDepth, gHitsFound[3] );
						}
						else
						{
							swprintf_s(msgString,1024,L"The current selection lower depth of %d contains hidden lower layers.\n\nDo you want to set the depth to %d to minimize the underground?\nSelect 'Cancel' to turn off this autocorrection system.",
								gTargetDepth, gHitsFound[3] );
						}
					}
                    retval = MessageBox( NULL, msgString,
                        _T("Informational"), MB_YESNOCANCEL|MB_ICONINFORMATION|MB_DEFBUTTON1);
                    if ( retval == IDYES )
                    {
                        gTargetDepth = gHitsFound[3];
						setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth );
                        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
                        // update target depth
                        SetHighlightState(gHighlightOn,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
						enableBottomControl( gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
                        draw();
                        InvalidateRect(hWnd,NULL,FALSE);
                        UpdateWindow(hWnd);
                    }
					else if ( retval == IDCANCEL )
					{
						gAutocorrectDepth = 0;
					}
                }
            }
        }
        break;
    case WM_MOUSEMOVE:
        if (gLoaded)
        {
            if (dragging)
            {
                // mouse coordinate can now be negative
                int mouseX = LOWORD(lParam);
                if ( mouseX > 0x7fff )
                    mouseX -= 0x10000;
                int mouseY = HIWORD(lParam);
                if ( mouseY > 0x7fff )
                    mouseY -= 0x10000;
                gCurZ-=(mouseY-oldY)/gCurScale;
                gCurX-=(mouseX-oldX)/gCurScale;
                oldX=mouseX;
                oldY=mouseY;
                draw();
                InvalidateRect(hWnd,NULL,FALSE);
                UpdateWindow(hWnd);
            }
            // for given mouse position and world center, determine
            // mx, mz, the world coordinates that the mouse is over,
            // and return the name of the block type it's over
            blockLabel=IDBlock(LOWORD(lParam),HIWORD(lParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
                    bitWidth,bitHeight,gCurScale,&mx,&my,&mz,&type);
            holdlParam=lParam;
            if (hdragging && gLoaded)
            {
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
                // change map center, in world coordinates, by mouse move
                if ( gLockMouseZ )
                {
                    gStartHiZ = minz;
                    mz = maxz;
                }
                if ( gLockMouseX )
                {
                    gStartHiX = minx;
                    mx = maxx;
                }
                // update highlight end to this position
                SetHighlightState(gHighlightOn,gStartHiX,gTargetDepth,gStartHiZ,mx,gCurDepth,mz);
				enableBottomControl( gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
                draw();
                InvalidateRect(hWnd,NULL,FALSE);
                UpdateWindow(hWnd);
            }
            updateStatus(mx,mz,my,blockLabel,hwndStatus);
        }
        break;
    case WM_KEYDOWN:
            // Check if control key is being held down. If so,
            // ignore this keypress and assume the .rc file has
            // emitted a corresponding message. Else control+S will
            // have the effect of saving and scrolling the map.
            if ( gLoaded && GetKeyState(VK_CONTROL) >= 0 )
            {
#ifdef _DEBUG
                wchar_t outputString[256];
                swprintf_s(outputString,256,L"key: %d\n",wParam);
                OutputDebugString( outputString );
#endif

                BOOL changed=FALSE;
                switch (wParam)
                {
                case VK_UP:
                case 'W':
                    moving|=1;
                    break;
                case VK_DOWN:
                case 'S':
                    moving|=2;
                    break;
                case VK_LEFT:
                case 'A':
                    moving|=4;
                    break;
                case VK_RIGHT:
                case 'D':
                    moving|=8;
                    break;
                case VK_PRIOR:
                case 'E':
                    gCurScale+=0.5; // 0.25*pow(gCurScale,1.2)/gCurScale;
                    if (gCurScale>MAXZOOM)
                        gCurScale=MAXZOOM;
                    changed=TRUE;
                    break;
                case VK_NEXT:
                case 'Q':
                    gCurScale-=0.5; // 0.25*pow(gCurScale,1.2)/gCurScale;
                    if (gCurScale<MINZOOM)
                        gCurScale=MINZOOM;
                    changed=TRUE;
                    break;
                // bottom: set depth to save down to (or up to)
                case 'B':
                    gTargetDepth = gCurDepth;
                    {
                        // also set highlight state to new depths
						setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth );
                        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
                        SetHighlightState(on,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
						enableBottomControl( on, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );

                        // holdlParam is just the last mouse position, so status bar is good.
                        blockLabel=IDBlock(LOWORD(holdlParam),HIWORD(holdlParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
                            bitWidth,bitHeight,gCurScale,&mx,&my,&mz,&type);
                        updateStatus(mx,mz,my,blockLabel,hwndStatus);
                        draw();
                        InvalidateRect(hWnd,NULL,FALSE);
                        UpdateWindow(hWnd);
                    }
                    break;
                // increment target depth by one
                case VK_OEM_4:    // [
                    gTargetDepth++;
                    gTargetDepth = clamp(gTargetDepth,0,MAP_MAX_HEIGHT);
					setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth );
                    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
                    SetHighlightState(on,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
					enableBottomControl( on, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
                    blockLabel=IDBlock(LOWORD(holdlParam),HIWORD(holdlParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
                        bitWidth,bitHeight,gCurScale,&mx,&my,&mz,&type);
                    updateStatus(mx,mz,my,blockLabel,hwndStatus);
                    draw();
                    InvalidateRect(hWnd,NULL,FALSE);
                    UpdateWindow(hWnd);
                    break;
                // decrement target depth by one
                case VK_OEM_6:    // ]
                    gTargetDepth--;
                    gTargetDepth = clamp(gTargetDepth,0,MAP_MAX_HEIGHT);
					setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth );
                    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
                    SetHighlightState(on,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
					enableBottomControl( on, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
                    blockLabel=IDBlock(LOWORD(holdlParam),HIWORD(holdlParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
                        bitWidth,bitHeight,gCurScale,&mx,&my,&mz,&type);
                    updateStatus(mx,mz,my,blockLabel,hwndStatus);
                    draw();
                    InvalidateRect(hWnd,NULL,FALSE);
                    UpdateWindow(hWnd);
                    break;
                case VK_OEM_PERIOD:
                case '.':
                case '>':
                    if (gCurDepth>0)
                    {
                        gCurDepth--;
                        setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                    }
                    break;
                case VK_OEM_COMMA:
                case ',':
                case '<':
                    if (gCurDepth<MAP_MAX_HEIGHT)
                    {
                        gCurDepth++;
                        setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                    }
                    break;
                case '0':
                    gCurDepth=0;
                    setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                    break;
                case '1':
                    gCurDepth=10;
                    setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                    break;
                case '2':
                    gCurDepth=20;
                    setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                    break;
                case '3':
                    gCurDepth=30;
                    setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                    break;
                case '4':
                    gCurDepth=40;
                    setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                    break;
                case '5':
                    gCurDepth=51;	// bottom dirt layer of deep lakes
                    setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                    break;
                case '6':
                    gCurDepth=SEA_LEVEL;
                    setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                    break;
                case '7':
                    gCurDepth=85;
                    setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                    break;
                case '8':
                    gCurDepth=106;
                    setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                    break;
                case '9':
                    gCurDepth=MAP_MAX_HEIGHT;
                    setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                    break;
                case VK_HOME:
                    gCurScale=MAXZOOM;
                    changed=TRUE;
                    break;
                case VK_END:
                    gCurScale=MINZOOM;
                    changed=TRUE;
                    break;
                default:
                    // unknown key, don't move
                    moving=0;
                    break;
                }
                if (moving!=0)
                {
                    if (moving&1) //up
                        gCurZ-=10.0/gCurScale;
                    if (moving&2) //down
                        gCurZ+=10.0/gCurScale;
                    if (moving&4) //left
                        gCurX-=10.0/gCurScale;
                    if (moving&8) //right
                        gCurX+=10.0/gCurScale;
                    changed=TRUE;
                }
                if (changed)
                {
                    draw();
                    InvalidateRect(hWnd,NULL,FALSE);
                    UpdateWindow(hWnd);
                }
            }
        break;
    case WM_KEYUP:
        switch (wParam)
        {
            case VK_UP:
            case 'W':
                moving&=~1;
                break;
            case VK_DOWN:
            case 'S':
                moving&=~2;
                break;
            case VK_LEFT:
            case 'A':
                moving&=~4;
                break;
            case VK_RIGHT:
            case 'D':
                moving&=~8;
                break;
        }
        break;
    case WM_HSCROLL:
		pos=(DWORD)SendMessage(hwndSlider,TBM_GETPOS,0,0);
		_itow_s(MAP_MAX_HEIGHT-pos,text,10);
		SetWindowText(hwndLabel,text);
		gCurDepth=MAP_MAX_HEIGHT-pos;

		pos=(DWORD)SendMessage(hwndBottomSlider,TBM_GETPOS,0,0);
		_itow_s(MAP_MAX_HEIGHT-pos,text,10);
		SetWindowText(hwndBottomLabel,text);
		gTargetDepth=MAP_MAX_HEIGHT-pos;

        syncCurrentHighlightDepth();

		GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
		SetHighlightState(on,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
		enableBottomControl( on, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
		blockLabel=IDBlock(LOWORD(holdlParam),HIWORD(holdlParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
			bitWidth,bitHeight,gCurScale,&mx,&my,&mz,&type);
		updateStatus(mx,mz,my,blockLabel,hwndStatus);

        draw();
        InvalidateRect(hWnd,NULL,FALSE);
        SetFocus(hWnd);
        UpdateWindow(hWnd);
        break;
    case WM_CTLCOLORSTATIC: //color the label and the slider background
        {
            HDC hdcStatic=(HDC)wParam;
            SetTextColor(hdcStatic,GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(hdcStatic,GetSysColor(COLOR_WINDOW));
            return (INT_PTR)ctlBrush;
        }
        break;
    case WM_COMMAND:
        wmId    = LOWORD(wParam);
        // set but not used: wmEvent = HIWORD(wParam);
        // Parse the menu selections:
        if (wmId>=IDM_CUSTOMCOLOR && wmId<IDM_CUSTOMCOLOR+1000)
            useCustomColor(wmId,hWnd);

		// load world
        if (wmId>IDM_WORLD && wmId<IDM_WORLD+999)
        {
			int loadErr;
            //convert path to utf8
            //WideCharToMultiByte(CP_UTF8,0,worlds[wmId-IDM_WORLD],-1,gWorld,MAX_PATH,NULL,NULL);
            gSameWorld = (wcscmp(gWorld,worlds[wmId-IDM_WORLD])==0);
            wcscpy_s(gWorld,MAX_PATH,worlds[wmId-IDM_WORLD]);
			loadErr = loadWorld();
            if ( loadErr )
            {
                // world not loaded properly
				if ( loadErr == 2 )
				{
					MessageBox( NULL, _T("Error: world has not been converted to the Anvil format.\nTo convert a world, run Minecraft 1.2 or later and play it, then quit.\nTo use Mineways on an old-style McRegion world, download\nVersion 1.15 from the mineways.com site."),
						_T("Read error"), MB_OK|MB_ICONERROR);
				}
				else
				{
					MessageBox( NULL, _T("Error: cannot read world."),
						_T("Read error"), MB_OK|MB_ICONERROR);
				}

                return 0;
            }
			EnableWindow(hwndSlider,TRUE);
			EnableWindow(hwndLabel,TRUE);
			EnableWindow(hwndInfoLabel,TRUE);
			EnableWindow(hwndBottomSlider,TRUE);
			EnableWindow(hwndBottomLabel,TRUE);
            InvalidateRect(hWnd,NULL,TRUE);
			setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
			setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth );
            UpdateWindow(hWnd);
            validateItems(GetMenu(hWnd));
        }
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_COLOR:
            {
                doColorSchemes(hInst,hWnd);
                populateColorSchemes(GetMenu(hWnd));
                // always go back to the standard color scheme after editing, as editing
                // could have removed the custom scheme being modified.
                wchar_t* schemeSelected = getSelectedColorScheme();
                if ( wcslen(schemeSelected) <= 0 )
                {
                    useCustomColor(IDM_CUSTOMCOLOR,hWnd);
                }
                else
                {
                    int item = findColorScheme(schemeSelected);
                    if ( item > 0 )
                    {
                        useCustomColor(IDM_CUSTOMCOLOR+item,hWnd);
                    }
                    else
                    {
                        assert(0);
                        useCustomColor(IDM_CUSTOMCOLOR,hWnd);
                    }
                }
            }
            break;
        case IDM_CLOSE:
            DestroyWindow(hWnd);
            break;
		case IDM_TEST_WORLD:
			gWorld[0] = 0;
			gSameWorld = 0;
			loadWorld();
			goto InitEnable;
		case IDM_WORLD:
        case IDM_OPEN:
            ZeroMemory(&ofn,sizeof(OPENFILENAME));
            ofn.lStructSize=sizeof(OPENFILENAME);
            ofn.hwndOwner=hWnd;
			wcscpy_s(path,MAX_PATH,gWorld);
            ofn.lpstrFile=path;
            //path[0]=0;
            ofn.nMaxFile=MAX_PATH;
            ofn.lpstrFilter=L"Minecraft World (level.dat)\0level.dat\0";
            ofn.nFilterIndex=1;
            ofn.lpstrFileTitle=NULL;
            ofn.nMaxFileTitle=0;
            ofn.lpstrInitialDir=NULL;
            ofn.Flags=OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
            if (GetOpenFileName(&ofn)==TRUE)
            {
                PathRemoveFileSpec(path);
                //convert path to utf8
                //WideCharToMultiByte(CP_UTF8,0,path,-1,gWorld,MAX_PATH,NULL,NULL);
                gSameWorld = (wcscmp(gWorld,path)==0);
                wcscpy_s(gWorld,MAX_PATH,path);
                if ( loadWorld() )
                {
                    // world not loaded properly
                    MessageBox( NULL, _T("Error: cannot read world."),
                        _T("Read error"), MB_OK|MB_ICONERROR);

                    return 0;
                }
			InitEnable:
				EnableWindow(hwndSlider,TRUE);
				EnableWindow(hwndLabel,TRUE);
				EnableWindow(hwndInfoLabel,TRUE);
				EnableWindow(hwndBottomSlider,TRUE);
				EnableWindow(hwndBottomLabel,TRUE);
                InvalidateRect(hWnd,NULL,TRUE);
				setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
				setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth );
                UpdateWindow(hWnd);
            }
            break;
        case IDM_FILE_SELECTTERRAIN:
            ZeroMemory(&ofn,sizeof(OPENFILENAME));
            ofn.lStructSize=sizeof(OPENFILENAME);
            ofn.hwndOwner=hWnd;
			wcscpy_s(path,MAX_PATH,gSelectTerrain);
            ofn.lpstrFile=path;
            //path[0]=0;
            ofn.nMaxFile=MAX_PATH;
            ofn.lpstrFilter=L"Terrain file terrain.png\0*.png\0";
            ofn.nFilterIndex=1;
            ofn.lpstrFileTitle=NULL;
            ofn.nMaxFileTitle=0;
            ofn.lpstrInitialDir=NULL;
            ofn.Flags=OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
            if (GetOpenFileName(&ofn)==TRUE)
            {
                // copy file name, since it definitely appears to exist.
                wcscpy_s(gSelectTerrain,MAX_PATH,path);
            }
            break;
        case IDM_FILE_PRINTOBJ:
		case IDM_FILE_SAVEOBJ:
            gPrintModel = (wmId==IDM_FILE_PRINTOBJ);
            {
                ZeroMemory(&ofn,sizeof(OPENFILENAME));
                ofn.lStructSize=sizeof(OPENFILENAME);
                ofn.hwndOwner=hWnd;
                ofn.lpstrFile=gExportPath;
                //gExportPath[0]=0;
                ofn.nMaxFile=MAX_PATH;
				ofn.lpstrFilter= gPrintModel ? L"Sculpteo: Wavefront OBJ, absolute (*.obj)\0*.obj\0Wavefront OBJ, relative (*.obj)\0*.obj\0i.materialise: Binary Materialise Magics STL stereolithography file (*.stl)\0*.stl\0Binary VisCAM STL stereolithography file (*.stl)\0*.stl\0ASCII text STL stereolithography file (*.stl)\0*.stl\0Shapeways: VRML 2.0 (VRML 97) file (*.wrl)\0*.wrl\0" :
											   L"Wavefront OBJ, absolute (*.obj)\0*.obj\0Wavefront OBJ, relative (*.obj)\0*.obj\0Binary Materialise Magics STL stereolithography file (*.stl)\0*.stl\0Binary VisCAM STL stereolithography file (*.stl)\0*.stl\0ASCII text STL stereolithography file (*.stl)\0*.stl\0VRML 2.0 (VRML 97) file (*.wrl)\0*.wrl\0";
                ofn.nFilterIndex=(gPrintModel ? gExportPrintData.fileType+1 : gExportViewData.fileType+1);
                ofn.lpstrFileTitle=NULL;
                ofn.nMaxFileTitle=0;
                ofn.lpstrInitialDir=NULL;
                ofn.lpstrTitle=gPrintModel ? L"Save Model for 3D Printing" :  L"Save Model for Rendering";
                ofn.Flags=OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                saveOK = GetSaveFileName(&ofn);
                // save file type selected, no matter what (even on cancel); we
                // always set it because even if someone cancels a save, he probably still
                // wanted the file type chosen.
                if ( gPrintModel )
                {
                    gExportPrintData.fileType = ofn.nFilterIndex-1;
                }
                else
                {
                    gExportViewData.fileType = ofn.nFilterIndex-1;
                }
                if ( saveOK )
                {
					// if we got this far, then previous export is off, and we also want to ask for dialog.
					gExported=0;
					gFileType = ofn.nFilterIndex-1;

		case IDM_FILE_REPEATPREVIOUSEXPORT:
                    gExported = saveObjFile(hWnd,gExportPath,gPrintModel,gFileType,gSelectTerrain,!gExported);

                    SetHighlightState(1, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal );
					enableBottomControl( 1, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
                    // put target depth to new depth set, if any
                    if ( gTargetDepth != gpEFD->maxyVal )
                    {
                        gTargetDepth = gpEFD->minyVal;
                    }
                    blockLabel=IDBlock(LOWORD(holdlParam),HIWORD(holdlParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
                        bitWidth,bitHeight,gCurScale,&mx,&my,&mz,&type);
                    updateStatus(mx,mz,my,blockLabel,hwndStatus);
					setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
					setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth );
                }
            }
            break;
        case IDM_JUMPSPAWN:
            gCurX=gSpawnX;
            gCurZ=gSpawnZ;
            if (gOptions.worldType&HELL)
            {
                gCurX/=8.0;
                gCurZ/=8.0;
            }
            draw();
            InvalidateRect(hWnd,NULL,TRUE);
            UpdateWindow(hWnd);
            break;
        case IDM_JUMPPLAYER:
            gCurX=gPlayerX;
            gCurZ=gPlayerZ;
            if (gOptions.worldType&HELL)
            {
                gCurX/=8.0;
                gCurZ/=8.0;
            }
            draw();
            InvalidateRect(hWnd,NULL,TRUE);
            UpdateWindow(hWnd);
            break;
        case IDM_VIEW_JUMPTOMODEL: // F4 - TODO document
            GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
            if ( on )
            {
                gCurX=(minx+maxx)/2;
                gCurZ=(minz+maxz)/2;
                if (gOptions.worldType&HELL)
                {
                    gCurX/=8.0;
                    gCurZ/=8.0;
                }
                draw();
                InvalidateRect(hWnd,NULL,TRUE);
                UpdateWindow(hWnd);
            }
            break;
        case IDM_SHOWALLOBJECTS:
            gOptions.worldType^=SHOWALL;
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.worldType&SHOWALL)?MF_CHECKED:MF_UNCHECKED);
            draw();
            InvalidateRect(hWnd,NULL,TRUE);
            UpdateWindow(hWnd);
            break;
        case IDM_LIGHTING:
            gOptions.worldType^=LIGHTING;
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.worldType&LIGHTING)?MF_CHECKED:MF_UNCHECKED);
            draw();
            InvalidateRect(hWnd,NULL,TRUE);
            UpdateWindow(hWnd);
            break;
        case IDM_CAVEMODE:
            gOptions.worldType^=CAVEMODE;
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.worldType&CAVEMODE)?MF_CHECKED:MF_UNCHECKED);
            draw();
            InvalidateRect(hWnd,NULL,TRUE);
            UpdateWindow(hWnd);
            break;
        case IDM_OBSCURED:
            gOptions.worldType^=HIDEOBSCURED;
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.worldType&HIDEOBSCURED)?MF_CHECKED:MF_UNCHECKED);
            draw();
            InvalidateRect(hWnd,NULL,TRUE);
            UpdateWindow(hWnd);
            break;
        case IDM_DEPTH:
            gOptions.worldType^=DEPTHSHADING;
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.worldType&DEPTHSHADING)?MF_CHECKED:MF_UNCHECKED);
            draw();
            InvalidateRect(hWnd,NULL,TRUE);
            UpdateWindow(hWnd);
            break;
        case IDM_RELOAD_WORLD:
            // reload world, if loaded
            if ( gLoaded )
            {
                if ( loadWorld() )
                {
                    // world not loaded properly
                    MessageBox( NULL, _T("Error: cannot read world."),
                        _T("Read error"), MB_OK|MB_ICONERROR);

                    return 0;
                }
				EnableWindow(hwndSlider,TRUE);
				EnableWindow(hwndLabel,TRUE);
				EnableWindow(hwndInfoLabel,TRUE);
				EnableWindow(hwndBottomSlider,TRUE);
				EnableWindow(hwndBottomLabel,TRUE);
                InvalidateRect(hWnd,NULL,TRUE);
				setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
				setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth );
                UpdateWindow(hWnd);
            }
            break;
        case IDM_HELL:
            gOptions.worldType^=HELL;
            // clear selection when you switch worlds
            gHighlightOn=FALSE;
            SetHighlightState(gHighlightOn,0,0,0,0,0,0);
			enableBottomControl( gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
            // change scale as needed
            if (gOptions.worldType&HELL)
            {
                gCurX/=8.0;
                gCurZ/=8.0;
                // it's useless to view Nether from MAP_MAX_HEIGHT
                if ( gCurDepth == MAP_MAX_HEIGHT )
                {
                    gCurDepth = 126;
					setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                }
                gOverworldHideStatus = gOptions.worldType&HIDEOBSCURED;
                gOptions.worldType |= HIDEOBSCURED;
                //gTargetDepth=0;
                // semi-useful, I'm not sure: zoom in when going to nether
                //gCurScale *= 8.0;
                //gCurScale = clamp(gCurScale,MINZOOM,MAXZOOM);
            }
            else
            {
                // back to the overworld
                gCurX*=8.0;
                gCurZ*=8.0;
                if ( gCurDepth == 126 )
                {
                    gCurDepth = MAP_MAX_HEIGHT;
					setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth );
                }
                // turn off obscured, then restore overworld's obscured status
                gOptions.worldType &= ~HIDEOBSCURED;
                gOptions.worldType |= gOverworldHideStatus;
                //gTargetDepth=MIN_OVERWORLD_DEPTH;

                // semi-useful, I'm not sure: zoom out when going back
                //gCurScale /= 8.0;
                //gCurScale = clamp(gCurScale,MINZOOM,MAXZOOM);
            }
            CheckMenuItem(GetMenu(hWnd),IDM_OBSCURED,(gOptions.worldType&HIDEOBSCURED)?MF_CHECKED:MF_UNCHECKED);
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.worldType&HELL)?MF_CHECKED:MF_UNCHECKED);
            if (gOptions.worldType&ENDER)
            {
                CheckMenuItem(GetMenu(hWnd),IDM_END,MF_UNCHECKED);
                gOptions.worldType&=~ENDER;
            }
            CloseAll();
            blockLabel=IDBlock(LOWORD(holdlParam),HIWORD(holdlParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
                bitWidth,bitHeight,gCurScale,&mx,&my,&mz,&type);
            updateStatus(mx,mz,my,blockLabel,hwndStatus);
            draw();
            InvalidateRect(hWnd,NULL,TRUE);
            UpdateWindow(hWnd);
            break;
        case IDM_END:
            gOptions.worldType^=ENDER;
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.worldType&ENDER)?MF_CHECKED:MF_UNCHECKED);
            if (gOptions.worldType&ENDER)
            {
                if (gOptions.worldType&HELL)
                {
                    gCurX*=8.0;
                    gCurZ*=8.0;
                    CheckMenuItem(GetMenu(hWnd),IDM_HELL,MF_UNCHECKED);
                    gOptions.worldType&=~HELL;
                }
            }
            CloseAll();
            draw();
            InvalidateRect(hWnd,NULL,TRUE);
            UpdateWindow(hWnd);
            break;
        case IDM_HELP_GIVEMEMOREMEMORY:
            // If you go full screen, you'll want more memory, if your computer can handle it.
            // TODO document
            gOptions.moreMemory = !gOptions.moreMemory;
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.moreMemory)?MF_CHECKED:MF_UNCHECKED);
            ChangeCache( gOptions.moreMemory ? 30000 : 15000 );  // was 15000 : 6000 - Sean said "make it bigger"
            draw();
            InvalidateRect(hWnd,NULL,TRUE);
            UpdateWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        validateItems(GetMenu(hWnd));
        break;
    case WM_ERASEBKGND:
        {
            hdc=(HDC)wParam;
            GetClipBox(hdc,&rect);
            rect.bottom=MAIN_WINDOW_TOP;
            HBRUSH hb=CreateSolidBrush(GetSysColor(COLOR_WINDOW));
            FillRect(hdc,&rect,hb);
            DeleteObject(hb);
        }
        break;
    case WM_PAINT:
        hdc = BeginPaint(hWnd, &ps);
        GetClientRect(hWnd,&rect);
        rect.top+=MAIN_WINDOW_TOP;
        if (hdcMem==NULL)
        {
            hdcMem=CreateCompatibleDC(hdc);
            SelectObject(hdcMem,bitmap);
        }
        BitBlt(hdc,0,MAIN_WINDOW_TOP,bitWidth,bitHeight,hdcMem,0,0,SRCCOPY);
        EndPaint(hWnd, &ps);
        break;
    case WM_SIZING: //window resizing
        GetClientRect(hWnd,&rect);
		SetWindowPos(hwndSlider,NULL,0,0,
			rect.right-rect.left-40-SLIDER_LEFT,30,SWP_NOMOVE|SWP_NOZORDER | SWP_NOACTIVATE);
		SetWindowPos(hwndBottomSlider,NULL,0,30,
			rect.right-rect.left-40-SLIDER_LEFT,30,SWP_NOMOVE|SWP_NOZORDER | SWP_NOACTIVATE);
		SetWindowPos(hwndLabel,NULL,rect.right-40,5,
			30,20,SWP_NOACTIVATE);
		SetWindowPos(hwndBottomLabel,NULL,rect.right-40,35,
			30,20,SWP_NOACTIVATE);

        break;
    case WM_SIZE: //resize window
        SendMessage(hwndStatus,WM_SIZE,0,0);
        SendMessage(hwndStatus,SB_GETRECT,1,(LPARAM)&rect);
        MoveWindow(progressBar,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,TRUE);

        GetClientRect(hWnd,&rect);
		SetWindowPos(hwndSlider,NULL,0,0,
			rect.right-rect.left-40-SLIDER_LEFT,30,SWP_NOMOVE|SWP_NOZORDER | SWP_NOACTIVATE);
		SetWindowPos(hwndBottomSlider,NULL,0,30,
			rect.right-rect.left-40-SLIDER_LEFT,30,SWP_NOMOVE|SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hwndLabel,NULL,rect.right-40,5,
            30,20,SWP_NOACTIVATE);
		SetWindowPos(hwndBottomLabel,NULL,rect.right-40,35,
			30,20,SWP_NOACTIVATE);
        rect.top+=MAIN_WINDOW_TOP;
        rect.bottom-=23;
        bitWidth=rect.right-rect.left;
        bitHeight=rect.bottom-rect.top;
        bmi.bmiHeader.biWidth=bitWidth;
        bmi.bmiHeader.biHeight=-bitHeight;
        if (bitmap!=NULL)
            DeleteObject(bitmap);
        bitmap=CreateDIBSection(NULL,&bmi,DIB_RGB_COLORS,(void **)&map,NULL,0);
        if (hdcMem!=NULL)
            SelectObject(hdcMem,bitmap);
        draw();
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    // This helps with the "mouse up outside the window" problem, where if you mouse
    // up while dragging and you're outside the frame, the mouse will get stuck in
    // dragging mode as the mouse up event won't get generated.
    // This fix does *not* help when you're still inside the frame but are in the
    // status strip or the height slider - it still happens there.
    //case WM_NCMOUSEMOVE:
    //	dragging=FALSE;
    //	hdragging=FALSE;
    //	break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

static void updateStatus(int mx, int mz, int my, const char *blockLabel, HWND hwndStatus)
{
    wchar_t buf[100];
    // if mx, my or mz are crazy, print dashes
    if ( my < -1 || my >= MAP_MAX_HEIGHT+1 )
    {
		//wsprintf(buf,L"%S \t\tBottom %d",blockLabel,gTargetDepth);
		wsprintf(buf,L"%S",blockLabel);
    }
    else
    {
        // In Nether, show corresponding overworld coordinates
        if ( gOptions.worldType&HELL)
            //wsprintf(buf,L"%d,%d; y=%d[%d,%d] %S \t\tBtm %d",mx,mz,my,mx*8,mz*8,blockLabel,gTargetDepth);
			wsprintf(buf,L"%d,%d; y=%d[%d,%d] %S",mx,mz,my,mx*8,mz*8,blockLabel);
        else
			//wsprintf(buf,L"%d,%d; y=%d %S \t\tBottom %d",mx,mz,my,blockLabel,gTargetDepth);
			wsprintf(buf,L"%d,%d; y=%d %S",mx,mz,my,blockLabel);
    }
    SendMessage(hwndStatus,SB_SETTEXT,0,(LPARAM)buf);
}


static int numCustom=1;
static void populateColorSchemes(HMENU menu)
{
    MENUITEMINFO info;
    for (int i=1;i<numCustom;i++)
        DeleteMenu(menu,IDM_CUSTOMCOLOR+i,MF_BYCOMMAND);
    info.cbSize=sizeof(MENUITEMINFO);
    info.fMask=MIIM_FTYPE|MIIM_ID|MIIM_STRING|MIIM_DATA;
    info.fType=MFT_STRING;
    numCustom=1;
    ColorManager cm;
    ColorScheme cs;
    int id=cm.next(0,&cs);
    while (id)
    {
        info.wID=IDM_CUSTOMCOLOR+numCustom;
        info.cch=(UINT)wcslen(cs.name);
        info.dwTypeData=cs.name;
        info.dwItemData=cs.id;
        InsertMenuItem(menu,IDM_CUSTOMCOLOR,FALSE,&info);
        numCustom++;
        id=cm.next(id,&cs);
    }
}
static void useCustomColor(int wmId,HWND hWnd)
{
    for (int i=0;i<numCustom;i++)
        CheckMenuItem(GetMenu(hWnd),IDM_CUSTOMCOLOR+i,MF_UNCHECKED);
    CheckMenuItem(GetMenu(hWnd),wmId,MF_CHECKED);
    ColorManager cm;
    ColorScheme cs;
    if (wmId>IDM_CUSTOMCOLOR)
    {
        MENUITEMINFO info;
        info.cbSize=sizeof(MENUITEMINFO);
        info.fMask=MIIM_DATA;
        GetMenuItemInfo(GetMenu(hWnd),wmId,FALSE,&info);
        cs.id=(int)info.dwItemData;
        cm.load(&cs);
    }
    else
        ColorManager::Init(&cs);
    SetMapPalette(cs.colors,256);
    SetExportPalette(cs.colors,256);
    draw();
    InvalidateRect(hWnd,NULL,TRUE);
    UpdateWindow(hWnd);
}
static int findColorScheme(wchar_t* name)
{
    ColorManager cm;
    ColorScheme cs;
    int id=cm.next(0,&cs);
    int count = 1;
    // go through list in same order as created in populateColorSchemes
    while (id)
    {
        if ( wcscmp( name, cs.name) == 0 )
            return count;
        id=cm.next(id,&cs);
        count++;
    }
    return -1;
}

static void updateProgress(float progress)
{
    SendMessage(progressBar,PBM_SETPOS,(int)(progress*100),0);
}

static void draw()
{
    if (gLoaded)
        DrawMap(gWorld,gCurX,gCurZ,gCurDepth,bitWidth,bitHeight,gCurScale,map,gOptions,gHitsFound,updateProgress);
    else
        memset(map,0xff,bitWidth*bitHeight*4);
    SendMessage(progressBar,PBM_SETPOS,0,0);
    for (int i=0;i<bitWidth*bitHeight*4;i+=4)
    {
        map[i]^=map[i+2];
        map[i+2]^=map[i];
        map[i]^=map[i+2];
    }
}

// return 1 if world could not be loaded
static int loadWorld()
{
	int version;
    CloseAll();

	if ( gWorld[0] == 0 )
	{
		// load test world
		gSpawnX = gSpawnY = gSpawnZ = gPlayerX = gPlayerY = gPlayerZ = 0;
	}
	else
	{
		// Don't clear selection! It's a feature: you can export, then go modify your Minecraft
		// world, then reload and carry on. TODO: document!
		//gHighlightOn=FALSE;
		//SetHighlightState(gHighlightOn,0,gTargetDepth,0,0,gCurDepth,0);
		if ( GetFileVersion(gWorld,&version) != 0 ) {
			return 1;
		}
		if ( version < 19133 )
		{
			// world is old
			return 2;
		}
		if ( GetSpawn(gWorld,&gSpawnX,&gSpawnY,&gSpawnZ) != 0 ) {
			return 1;
		}
		GetPlayer(gWorld,&gPlayerX,&gPlayerY,&gPlayerZ);
	}

    // if this is the first world you loaded, or not the same world as before (reload), set location to spawn.
    if ( !gSameWorld )
    {
        gCurX=gSpawnX;
        gCurZ=gSpawnZ;
        gSameWorld=1;   // so if we reload
        // zoom out when loading a new world, since location's reset.
        gCurScale=MINZOOM;

		gCurDepth = MAP_MAX_HEIGHT;
		gTargetDepth = MIN_OVERWORLD_DEPTH;
		gHighlightOn=FALSE;
		SetHighlightState(gHighlightOn,0,gTargetDepth,0,0,gCurDepth,0);
    }
    gLoaded=TRUE;
    draw();
    return 0;
}

static void worldPath(TCHAR *path)
{
    SHGetFolderPath(NULL,CSIDL_APPDATA,NULL,0,path);
    PathAppend(path,L".minecraft");
    PathAppend(path,L"saves");
}

static int loadWorldList(HMENU menu)
{
	int oldVersionDetected = 0;
    MENUITEMINFO info;
    info.cbSize=sizeof(MENUITEMINFO);
    info.fMask=MIIM_FTYPE|MIIM_ID|MIIM_STRING|MIIM_DATA;
    info.fType=MFT_STRING;
    TCHAR path[MAX_PATH];
    HANDLE hFind;
    WIN32_FIND_DATA ffd;
    int numWorlds=1;

    worldPath(path);
    PathAppend(path,L"*");
    hFind=FindFirstFile(path,&ffd);

    do
    {
        if (ffd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
        {
            if (ffd.cFileName[0]!='.')
            {
				// test if world is in Anvil format
				int version;
				TCHAR testAnvil[MAX_PATH];
				worldPath(testAnvil);
				PathAppend(testAnvil,ffd.cFileName);
				if ( GetFileVersion(testAnvil,&version) != 0 )
				{
					// unreadable world, for some reason - couldn't even read version
					continue;
				}

                info.wID=IDM_WORLD+numWorlds;
                info.cch=(UINT)wcslen(ffd.cFileName);
                info.dwTypeData=ffd.cFileName;
                info.dwItemData=numWorlds;
				// if version is pre-Anvil, show world but gray it out
				if (version < 19133)
				{
					oldVersionDetected = 1;
					// gray it out
					info.fMask |= MIIM_STATE;
					info.fState = MFS_DISABLED;
				}
				else
				{
					//info.fMask |= MIIM_STATE;
					info.fState = 0x0; // MFS_CHECKED;
				}
                InsertMenuItem(menu,IDM_TEST_WORLD,FALSE,&info);
                worlds[numWorlds]=(TCHAR*)malloc(sizeof(TCHAR)*MAX_PATH);
                worldPath(worlds[numWorlds]);
                PathAppend(worlds[numWorlds],ffd.cFileName);
                numWorlds++;
            }
        }
    } while (FindNextFile(hFind,&ffd)!=0);
	return oldVersionDetected;
}

static void enableBottomControl( int state, HWND hwndBottomSlider, HWND hwndBottomLabel, HWND hwndInfoBottomLabel )
{
	if ( state != gBottomControlEnabled )
	{
		gBottomControlEnabled = state;
		// Currently only the label "Lower depth" is affected by selection;
		// in this way the depth can be modified even if selection is not on. The problem with an inactive
		// slider is that the map window underneath is then active instead, and you'll drag the map.
		//EnableWindow(hwndBottomSlider,state);
		//EnableWindow(hwndBottomLabel,state);
		hwndBottomSlider;
		hwndBottomLabel;
		EnableWindow(hwndInfoBottomLabel,state);
	}
}


// validate menu items
static void validateItems(HMENU menu)
{
    // gray out options that are not available
    if (gLoaded)
    {
        EnableMenuItem(menu,IDM_JUMPSPAWN,MF_ENABLED);
        EnableMenuItem(menu,IDM_JUMPPLAYER,MF_ENABLED);
        EnableMenuItem(menu,IDM_FILE_SAVEOBJ,gHighlightOn?MF_ENABLED:MF_DISABLED);
        EnableMenuItem(menu,IDM_FILE_PRINTOBJ,gHighlightOn?MF_ENABLED:MF_DISABLED);
        EnableMenuItem(menu,IDM_VIEW_JUMPTOMODEL,gHighlightOn?MF_ENABLED:MF_DISABLED);
    }
    else
    {
        EnableMenuItem(menu,IDM_JUMPSPAWN,MF_DISABLED);
        EnableMenuItem(menu,IDM_JUMPPLAYER,MF_DISABLED);
        EnableMenuItem(menu,IDM_FILE_SAVEOBJ,MF_DISABLED);
        EnableMenuItem(menu,IDM_FILE_PRINTOBJ,MF_DISABLED);
        EnableMenuItem(menu,IDM_VIEW_JUMPTOMODEL,MF_DISABLED);
    }
	// has a save been done?
	if (gExported)
	{
		EnableMenuItem(menu,IDM_FILE_REPEATPREVIOUSEXPORT,MF_ENABLED);
	}
	else
	{
		EnableMenuItem(menu,IDM_FILE_REPEATPREVIOUSEXPORT,MF_DISABLED);
	}
}

static void setSlider( HWND hWnd, HWND hwndSlider, HWND hwndLabel, int depth )
{
	syncCurrentHighlightDepth();

	wchar_t text[4];
	SendMessage(hwndSlider,TBM_SETPOS,1,MAP_MAX_HEIGHT-depth);
	_itow_s(depth,text,10);
	SetWindowText(hwndLabel,text);
	draw();
	InvalidateRect(hWnd,NULL,FALSE);
	UpdateWindow(hWnd);
}

static void syncCurrentHighlightDepth()
{
    // changing the depth 
    int on, minx, miny, minz, maxx, maxy, maxz;
    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
    // wherever the target depth is, put it back in that place and replace the other
    if ( maxy == gTargetDepth )
    {
        // the rare case, where target depth is larger than current depth
        SetHighlightState(on, minx, gCurDepth, minz, maxx, gTargetDepth, maxz);
    }
    else
    {
        SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz);
    }
}

// returns number of files written on successful export, 0 files otherwise.
static int saveObjFile( HWND hWnd, wchar_t *objFileName, BOOL printModel, int fileType, wchar_t *terrainFileName, BOOL showDialog )
{
    int on;
    int retCode = 0;

    if ( printModel )
    {
        gpEFD = &gExportPrintData;
        gOptions.exportFlags = EXPT_3DPRINT;
    }
    else
    {
        gpEFD = &gExportViewData;
        gOptions.exportFlags = 0x0;
    }
    gOptions.pEFD = gpEFD;

    // to use a preset set of values above, set this true, or break here and jump to line after "if"
    static int gDebugSetBlock=0;
    if ( gDebugSetBlock )
        SetHighlightState( 1, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal );

    // normal output
    GetHighlightState(&on, &gpEFD->minxVal, &gpEFD->minyVal, &gpEFD->minzVal, &gpEFD->maxxVal, &gpEFD->maxyVal, &gpEFD->maxzVal );

    int miny = gpEFD->minyVal;
    int maxy = gpEFD->maxyVal;

    setExportPrintData(gpEFD);

    if ( showDialog && !doExportPrint(hInst,hWnd) )
    {
        // canceled, so cancel output
        return 0;
    }

    getExportPrintData(gpEFD);  // could be unchanged, on cancel, but that's OK

    // if user changed depths
    if ( miny != gpEFD->minyVal || maxy != gpEFD->maxyVal )
    {
        // see if target did not change
        if ( gTargetDepth <= gCurDepth )
        {
            gTargetDepth = gpEFD->minyVal;
            gCurDepth = gpEFD->maxyVal;
        }
        else
        {
            gTargetDepth = gpEFD->maxyVal;
            gCurDepth = gpEFD->minyVal;
        }
    }
    SetHighlightState(on, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal );

    // export all
    if ( gpEFD->chkExportAll )
    {
        if ( printModel )
        {
            gOptions.saveFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTOP |
                BLF_FLATSIDE | BLF_3D_BIT;
        }
        else
        {
            gOptions.saveFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTOP |
                BLF_FLATSIDE | BLF_SMALL_MIDDLER | BLF_SMALL_BILLBOARD;
        }
    }
    else
    {
        gOptions.saveFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTOP | BLF_FLATSIDE;
    }

    // export options
    if ( gpEFD->radioExportMtlColors[gpEFD->fileType] == 1 )
    {
        gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_GROUP_BY_MATERIAL;
    }
    else if ( gpEFD->radioExportSolidTexture[gpEFD->fileType] == 1 )
    {
        gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_SWATCHES | EXPT_GROUP_BY_MATERIAL;
    }
    else if ( gpEFD->radioExportFullTexture[gpEFD->fileType] == 1 )
    {
        gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_GROUP_BY_MATERIAL;
        // TODO: if we're *viewing* full textures, output all the billboards!
        //  gOptions.saveFilterFlags |= BLF_SMALL_BILLBOARD;
    }

    gOptions.exportFlags |=
        (gpEFD->chkFillBubbles ? EXPT_FILL_BUBBLES : 0x0) |
        ((gpEFD->chkFillBubbles&&gpEFD->chkSealEntrances) ? EXPT_FILL_BUBBLES|EXPT_SEAL_ENTRANCES : 0x0) |
        ((gpEFD->chkFillBubbles&&gpEFD->chkSealSideTunnels) ? EXPT_FILL_BUBBLES|EXPT_SEAL_SIDE_TUNNELS : 0x0) |

        (gpEFD->chkConnectParts ? EXPT_CONNECT_PARTS : 0x0) |
        // is it better to force part connection on if corner tips or edges are on? I feel like the
        // dialog should take care of this, not allowing these options if the controlling algorithm is set.
        (gpEFD->chkConnectCornerTips ? (EXPT_CONNECT_PARTS|EXPT_CONNECT_CORNER_TIPS) : 0x0) |
        (gpEFD->chkConnectAllEdges ? (EXPT_CONNECT_PARTS|EXPT_CONNECT_ALL_EDGES) : 0x0) |
        (gpEFD->chkDeleteFloaters ? EXPT_DELETE_FLOATING_OBJECTS : 0x0) |

        (gpEFD->chkHollow ? EXPT_HOLLOW_BOTTOM : 0x0) |
        ((gpEFD->chkHollow && gpEFD->chkSuperHollow) ? EXPT_HOLLOW_BOTTOM|EXPT_SUPER_HOLLOW_BOTTOM : 0x0) |

        // materials are forced on if using debugging mode - just an internal override, doesn't need to happen in dialog.
        (gpEFD->chkShowParts ? EXPT_DEBUG_SHOW_GROUPS|EXPT_OUTPUT_MATERIALS|EXPT_OUTPUT_OBJ_GROUPS|EXPT_OUTPUT_OBJ_MATERIAL_PER_TYPE : 0x0) |
        (gpEFD->chkShowWelds ? EXPT_DEBUG_SHOW_WELDS|EXPT_OUTPUT_MATERIALS|EXPT_OUTPUT_OBJ_GROUPS|EXPT_OUTPUT_OBJ_MATERIAL_PER_TYPE : 0x0);


	// set OBJ group and material output state
	if ( fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || fileType == FILE_TYPE_WAVEFRONT_REL_OBJ )
	{
		if ( gpEFD->chkMultipleObjects )
		{
			// note, can get overridden by EXPT_GROUP_BY_BLOCK being on.
			gOptions.exportFlags |= EXPT_OUTPUT_OBJ_GROUPS;

			if ( gpEFD->chkMaterialPerType )
			{
				gOptions.exportFlags |= EXPT_OUTPUT_OBJ_MATERIAL_PER_TYPE;

				if ( gpEFD->chkG3DMaterial )
				{
					// G3D
					gOptions.exportFlags |= EXPT_OUTPUT_OBJ_NEUTRAL_MATERIAL|EXPT_OUTPUT_OBJ_FULL_MATERIAL;
				}
			}
		}
		// if in debugging mode, force groups and material type
	}
	// check if we're exporting relative coordinates
    if ( fileType == FILE_TYPE_WAVEFRONT_REL_OBJ )
    {
        gOptions.exportFlags |= EXPT_OUTPUT_OBJ_REL_COORDINATES;
    }
	// STL files never need grouping by material, and certainly don't export textures
    else if ( fileType == FILE_TYPE_ASCII_STL )
    {
        int unsupportedCodes = (EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_SWATCHES | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_GROUP_BY_MATERIAL|
            EXPT_DEBUG_SHOW_GROUPS|EXPT_DEBUG_SHOW_WELDS);
        if ( gOptions.exportFlags & unsupportedCodes )
        {
            MessageBox( NULL, _T("Note: color output is not supported for ASCII text STL.\nFile will contain no colors."),
                _T("Informational"), MB_OK|MB_ICONINFORMATION);
        }
        // ASCII STL in particular cannot export any materials at all.
        gOptions.exportFlags &= ~unsupportedCodes;

        // we never have to group by material for STL, as there are no material groups.
        gOptions.exportFlags &= ~EXPT_GROUP_BY_MATERIAL;
    }
    else if ( ( fileType == FILE_TYPE_BINARY_MAGICS_STL ) || ( fileType == FILE_TYPE_BINARY_VISCAM_STL ) )
    {
        int unsupportedCodes = (EXPT_OUTPUT_TEXTURE_SWATCHES | EXPT_OUTPUT_TEXTURE_IMAGES);
        if ( gOptions.exportFlags & unsupportedCodes )
        {
            if ( fileType == FILE_TYPE_BINARY_VISCAM_STL )
            {
                MessageBox( NULL, _T("Note: texture output is not supported for binary STL.\nFile will contain VisCAM colors instead."),
                    _T("Informational"), MB_OK|MB_ICONINFORMATION);
            }
            else
            {
                MessageBox( NULL, _T("Note: texture output is not supported for binary STL.\nFile will contain Materialise Magics colors instead."),
                    _T("Informational"), MB_OK|MB_ICONINFORMATION);
            }
        }
        gOptions.exportFlags &= ~unsupportedCodes;

        // we never have to group by material for STL, as there are no material groups.
        gOptions.exportFlags &= ~EXPT_GROUP_BY_MATERIAL;
    }
    else if ( fileType == FILE_TYPE_VRML2 )
    {
		// are we outputting color textures?
        if ( gOptions.exportFlags & EXPT_OUTPUT_TEXTURE )
        {
			if ( gOptions.exportFlags & EXPT_3DPRINT)
			{
				// if printing, we don't need to group by material, as it can be one huge pile of data
				gOptions.exportFlags &= ~EXPT_GROUP_BY_MATERIAL;
			}
			// else if we're outputting for rendering, VRML then outputs grouped by material, unless it's a single material output
			// (in which case this flag isn't turned on anyway).
        }
    }

	// if individual blocks are to be exported, we group by cube, not by material, so turn that off
	if ( gpEFD->chkIndividualBlocks )
	{
		// this also allows us to use the faceIndex as a way of noting the start of a new group
		gOptions.exportFlags &= ~EXPT_GROUP_BY_MATERIAL;
		gOptions.exportFlags |= EXPT_GROUP_BY_BLOCK;
	}

    // if showing debug groups, we need to turn off full image texturing so we get the largest group as semitransparent
    // (and full textures would just be confusing for debug, anyway)
    if ( gOptions.exportFlags & EXPT_DEBUG_SHOW_GROUPS )
    {
        if ( gOptions.exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES )
        {
            gOptions.exportFlags &= ~EXPT_OUTPUT_TEXTURE_IMAGES;
            gOptions.exportFlags |= EXPT_OUTPUT_TEXTURE_SWATCHES;
        }
		// we don't want to group by block for debugging
		gOptions.exportFlags &= ~EXPT_GROUP_BY_BLOCK;
    }

    // OK, all set, let's go!
    FileList outputFileList;
    outputFileList.count = 0;
    if ( on ) {
        // redraw, in case the bounds were changed
        draw();
        InvalidateRect(hWnd,NULL,FALSE);
        UpdateWindow(hWnd);

        int errCode = SaveVolume( objFileName, fileType, &gOptions, gWorld, gCurrentDirectory,
            gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal,
            updateProgress, terrainFileName, &outputFileList );

		// note how many files were output
		retCode = outputFileList.count;

        // zip it up - test that there's something to zip, in case of errors. Note that the first
        // file saved in ObjManip.c is the one used as the zip file's name.
        if ( gpEFD->chkCreateZip[gpEFD->fileType] && (outputFileList.count > 0) )
        {
            wchar_t wcZip[MAX_PATH];
            // we add .zip not (just) out of laziness, but this helps differentiate obj from wrl from stl.
            swprintf_s(wcZip,MAX_PATH,L"%s.zip",outputFileList.name[0]);

            DeleteFile(wcZip);
            HZIP hz = CreateZip(wcZip,0);
            for ( int i = 0; i < outputFileList.count; i++ )
            {
                const wchar_t *nameOnly = removePath( outputFileList.name[i] ) ;

                if (*updateProgress)
                { (*updateProgress)(0.75f + 0.25f*(float)i/(float)outputFileList.count);}

                ZipAdd(hz,nameOnly, outputFileList.name[i]);

				// delete model files if not needed
				if ( !gpEFD->chkCreateModelFiles[gpEFD->fileType] )
				{
					DeleteFile(outputFileList.name[i]);
				}
            }
            CloseZip(hz);
        }
        if (*updateProgress)
        { (*updateProgress)(1.0f);}


        // output stats, if printing and there *are* stats
        if ( gShowPrintStats && printModel && gOptions.cost > 0.0f && outputFileList.count > 0 )
        {
            int retval;
            wchar_t msgString[2000];
            swprintf_s(msgString,2000,L"3D Print Statistics:\n\nApproximate cost is $ %0.2f\nBase is %d x %d blocks, %d blocks high\nInches: base is %0.1f x %0.1f inches, %0.1f inches high\nCentimeters: Base is %0.1f x %0.1f cm, %0.1f cm high\nTotal number of blocks: %d\n\nDo you want to have statistics continue to be\ndisplayed on each export for this session?",
                gOptions.cost,
                gOptions.dimensions[0], gOptions.dimensions[2], gOptions.dimensions[1], 
                gOptions.dim_inches[0], gOptions.dim_inches[2], gOptions.dim_inches[1], 
                gOptions.dim_cm[0], gOptions.dim_cm[2], gOptions.dim_cm[1], 
                gOptions.totalBlocks );
            retval = MessageBox( NULL, msgString,
                _T("Informational"), MB_YESNO|MB_ICONINFORMATION|MB_DEFBUTTON1);
            if ( retval != IDYES )
            {
                gShowPrintStats = 0;
            }
        }

        if ( errCode != MW_NO_ERROR )
        {
            // pop up all errors flagged
            for ( int errNo = MW_NUM_CODES-1; errNo >= 0; errNo-- )
            {
                if ( (1<<errNo) & errCode )
                {
                    //int msgboxID = 
                    MessageBox(
                        NULL,
                        gPopupInfo[errNo+1].text,
                        gPopupInfo[errNo+1].caption,
                        gPopupInfo[errNo+1].type
                        );

                    //switch (msgboxID)
                    //{
                    //case IDCANCEL:
                    //    break;
                    //case IDTRYAGAIN:
                    //    break;
                    //case IDCONTINUE:
                    //    break;

                    //}
                }
            }
        }
    }

    return retCode;
}

// yes, this it totally lame, copying code from MinewaysMap
static const wchar_t *removePath( const wchar_t *src )
{
    // find last \ in string
    const wchar_t *strPtr = wcsrchr(src,(int)'\\');
    if ( strPtr )
        // found a \, so move up past it
        strPtr++;
    else
    {
        // look for /
        strPtr = wcsrchr(src,(int)'/');
        if ( strPtr )
            // found a /, so move up past it
            strPtr++;
        else
            // no \ or / found, just return string itself
            return src;
    }

    return strPtr;
}

#define INIT_ALL_FILE_TYPES( a, v0,v1,v2,v3,v4,v5)    \
    (a)[FILE_TYPE_WAVEFRONT_REL_OBJ] = (v0);    \
    (a)[FILE_TYPE_WAVEFRONT_ABS_OBJ] = (v1);    \
    (a)[FILE_TYPE_BINARY_MAGICS_STL] = (v2);    \
    (a)[FILE_TYPE_BINARY_VISCAM_STL] = (v3);    \
    (a)[FILE_TYPE_ASCII_STL] = (v4);    \
    (a)[FILE_TYPE_VRML2] = (v5);

static void initializeExportDialogData()
{
    // by default, make everything 0 - off
    memset(&gExportPrintData,0,sizeof(ExportFileData));

    // turn stuff on
    gExportPrintData.fileType = FILE_TYPE_VRML2;

	INIT_ALL_FILE_TYPES( gExportPrintData.chkCreateZip,         1, 1, 0, 0, 0, 1);
	// I used to set the last value to 0, meaning only the zip would be created. The idea
	// was that the naive user would then only have the zip, and so couldn't screw up
	// when uploading the model file. But this setting is a pain if you want to preview
	// the model file, you have to always remember to check the box so you can get the
	// preview files. So, now it's off.
	INIT_ALL_FILE_TYPES( gExportPrintData.chkCreateModelFiles,  1, 1, 1, 1, 1, 1);

    // Only VRML2 imports into Shapeways with color
    // order: OBJ, BSTL, ASTL, VRML
    INIT_ALL_FILE_TYPES( gExportPrintData.radioExportNoMaterials,  0, 0, 0, 0, 1, 0);
    // might as well export color with OBJ and binary STL - nice for previewing
    INIT_ALL_FILE_TYPES( gExportPrintData.radioExportMtlColors,    0, 0, 1, 1, 0, 0);  
    INIT_ALL_FILE_TYPES( gExportPrintData.radioExportSolidTexture, 0, 0, 0, 0, 0, 0);  
    INIT_ALL_FILE_TYPES( gExportPrintData.radioExportFullTexture,  1, 1, 0, 0, 0, 1);  

    gExportPrintData.chkMergeFlattop = 1;
    // Shapeways imports VRML files and displays them with Y up, that is, it
    // rotates them itself. Sculpteo imports OBJ, and likes Z is up, so we export with this on.
	// STL uses Z is up, even though i.materialise's previewer shows Y is up.
    INIT_ALL_FILE_TYPES( gExportPrintData.chkMakeZUp, 1, 1, 1, 1, 1, 0);  
    gExportPrintData.chkCenterModel = 1;
	gExportPrintData.chkIndividualBlocks = 0;

    gExportPrintData.radioRotate0 = 1;

    gExportPrintData.radioScaleByBlock = 1;
    gExportPrintData.modelHeightVal = 5.0f;    // 5 cm target height
    INIT_ALL_FILE_TYPES( gExportPrintData.blockSizeVal,
        METERS_TO_MM * mtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * mtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * mtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * mtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * mtlCostTable[PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE].minWall,
        METERS_TO_MM * mtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall);
    gExportPrintData.costVal = 25.00f;

    gExportPrintData.chkSealEntrances = 0; // left off by default: useful, but the user should want to do this
    gExportPrintData.chkSealSideTunnels = 0; // left off by default: useful, but the user should want to do this
    gExportPrintData.chkFillBubbles = 1;
    gExportPrintData.chkConnectParts = 1;
    gExportPrintData.chkConnectCornerTips = 1;
    // it's actually better to start with manifold off and see if there are lots of groups.
    gExportPrintData.chkConnectAllEdges = 0;
    gExportPrintData.chkDeleteFloaters = 1;
    gExportPrintData.chkHollow = 1;
	gExportPrintData.chkSuperHollow = 1;
	gExportPrintData.chkMeltSnow = 0;

	gExportPrintData.chkShowParts = 0;
	gExportPrintData.chkShowWelds = 0;

	gExportPrintData.chkMultipleObjects = 1;
	gExportPrintData.chkMaterialPerType = 1;
	gExportPrintData.chkG3DMaterial = 0;

    gExportPrintData.floaterCountVal = 16;
    INIT_ALL_FILE_TYPES( gExportPrintData.hollowThicknessVal,
        METERS_TO_MM * mtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * mtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * mtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * mtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * mtlCostTable[PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE].minWall,
        METERS_TO_MM * mtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall);

    // materials selected
    INIT_ALL_FILE_TYPES( gExportPrintData.comboPhysicalMaterial,1,1,1,1,0,1);
    // defaults: for Sculpteo OBJ, cm; for i.materialise, mm; for other STL, cm; for Shapeways VRML, m
    INIT_ALL_FILE_TYPES( gExportPrintData.comboModelUnits,UNITS_CENTIMETER,UNITS_CENTIMETER,UNITS_MILLIMETER,UNITS_MILLIMETER,UNITS_MILLIMETER,UNITS_METER);
 

    //////////////////////////////////////////////////////
    // copy view data from print, and change what's needed
    gExportViewData = gExportPrintData;

    // Now that I've figured out that Blender can show materials OK, change to "true spec"
    gExportViewData.fileType = FILE_TYPE_WAVEFRONT_ABS_OBJ;

    // don't really need to create a zip for rendering output
	INIT_ALL_FILE_TYPES( gExportViewData.chkCreateZip,         0, 0, 0, 0, 0, 0);
	INIT_ALL_FILE_TYPES( gExportViewData.chkCreateModelFiles,  1, 1, 1, 1, 1, 1);

    INIT_ALL_FILE_TYPES( gExportViewData.radioExportNoMaterials,  0, 0, 0, 0, 1, 0);  
    INIT_ALL_FILE_TYPES( gExportViewData.radioExportMtlColors,    0, 0, 1, 1, 0, 0);  
    INIT_ALL_FILE_TYPES( gExportViewData.radioExportSolidTexture, 0, 0, 0, 0, 0, 0);  
    INIT_ALL_FILE_TYPES( gExportViewData.radioExportFullTexture,  1, 1, 0, 0, 0, 1);  

    gExportViewData.chkExportAll = 1; 
	// for renderers, assume Y is up, which is the norm
    INIT_ALL_FILE_TYPES( gExportViewData.chkMakeZUp, 0, 0, 0, 0, 0, 0);  

    gExportViewData.modelHeightVal = 1000.0f;    // 10 cm - view doesn't need a minimum, really
    INIT_ALL_FILE_TYPES( gExportViewData.blockSizeVal,
        100.0f,
        100.0f,
        100.0f,
        100.0f,
        100.0f,
        100.0f);
    gExportViewData.costVal = 25.00f;

    gExportViewData.chkSealEntrances = 0;
    gExportViewData.chkSealSideTunnels = 0;
    gExportViewData.chkFillBubbles = 0;
    gExportViewData.chkConnectParts = 0;
    gExportViewData.chkConnectCornerTips = 0;
    gExportViewData.chkConnectAllEdges = 0;
    gExportViewData.chkDeleteFloaters = 0;
    gExportViewData.chkHollow = 0;
    gExportViewData.chkSuperHollow = 0;

    gExportViewData.floaterCountVal = 16;
    // irrelevant for viewing
    INIT_ALL_FILE_TYPES( gExportViewData.hollowThicknessVal, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f );    // 10 mm
    INIT_ALL_FILE_TYPES( gExportViewData.comboPhysicalMaterial,1,1,1,1,0,1);
    INIT_ALL_FILE_TYPES( gExportViewData.comboModelUnits,UNITS_METER,UNITS_METER,UNITS_MILLIMETER,UNITS_MILLIMETER,UNITS_MILLIMETER,UNITS_METER);
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

