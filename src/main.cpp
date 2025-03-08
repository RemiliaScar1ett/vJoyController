#include<iostream> 
#include"stdafx.h"
#include<vjoyinterface.h>
#include<unordered_map>
#include<queue>
#include<mutex>
#include<condition_variable>
#include<thread>

#define UNICODE
#define _UNICODE

#define ID_LX_LEFT  1001
#define ID_LX_RIGHT 1002
#define ID_LY_UP    1003
#define ID_LY_DOWN  1004
#define ID_LT       1005
#define ID_RT       1006
#define ID_BTN_A    1007
#define ID_BTN_B    1008
#define ID_BTN_X    1009
#define ID_BTN_Y    1010
#define ID_BTN_LB   1011
#define ID_BTN_RB   1012
#define ID_DPAD_UP   1017
#define ID_DPAD_DOWN 1018
#define ID_DPAD_LEFT 1019
#define ID_DPAD_RIGHT 1020

#define ID_LOG_BOX  2000

#define DEV_ID 1

int DEV_USABLE = 1;


#pragma region Declaration
enum class InputActType
{
    Button, AxisMove
};
struct InputAct
{
    int butID;
    bool isPressed;
    InputActType type;
};
std::queue<InputAct> inputQue;
std::mutex inputQueMutex;
std::condition_variable inputCV;
bool inputThreadRunning;
int CheckVJD(int dev_id);
void PushAction(const InputAct& Act);
void workThreadFunc();
void ActionProc(InputAct Act);
void ResettVJD(int dev_id);


HWND hLogBox;
std::queue<std::string> logBuff;//消息缓冲区
std::mutex logMutex;//日志线程锁
std::condition_variable logCondition;//通知器
bool logThreadRunning=TRUE;
void LogThread();
void LogMessage(const char* format, ...);
LRESULT CALLBACK AxisButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK presButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void Subclassing(HWND hwnd, WNDPROC tarProc);
HWND CreateButton(HWND hwnd, const TCHAR* text, int x,int y,int id);
void CreateGUI(HWND hwnd);
#pragma endregion


#pragma region Vjoy
//设备检查
int CheckVJD(int dev_id)
{
    if(!vJoyEnabled())
    {
        LogMessage("vJoy has not been enabled!");
        return -2;
    }
    VjdStat status = GetVJDStatus(dev_id);
    switch (status)
    {
        case VJD_STAT_OWN:
        {
            LogMessage("vJoy Device %d is already owned.", dev_id);
            break;
        }
        case VJD_STAT_FREE:
        {
            LogMessage("vJoy Device %d is free.",dev_id);
            break;
        }
        case VJD_STAT_BUSY:
        {
            LogMessage("[ERROR] vJoy Device %d is busy.",dev_id);
            return -1;
        }
        case VJD_STAT_MISS:
        {
            LogMessage("[ERROR] vJoy Device %d is missed.",dev_id);
            return -1;
        }
        default:
        {
            LogMessage("[ERROR] Unknown error.");
            return -1;
        }
    };
    
    BOOL AxisX = GetVJDAxisExist(DEV_ID, HID_USAGE_X);
    BOOL AxisY = GetVJDAxisExist(DEV_ID, HID_USAGE_Y);
    if(!AxisX)
    {
        LogMessage("[ERROR] AxisX does not exist.");
        return -1;
    }
    if(!AxisY)
    {
        LogMessage("[ERROR] AxisY does not exist.");
        return -1;
    }
    int nButtons = GetVJDButtonNumber(dev_id);
    if(status == VJD_STAT_FREE && !AcquireVJD(dev_id))
    {
        LogMessage("[ERROR] Failed to acquire.");
        return 0;
    }
    LogMessage("Acquire successfully.");
    return 1;

    ResettVJD(DEV_ID);
    
}

void ResettVJD(int dev_id)
{
    ResetVJD(dev_id);
    SetAxis(0x4000,DEV_ID,HID_USAGE_X);
    SetAxis(0x4000,DEV_ID,HID_USAGE_RX);
    SetAxis(0x4000,DEV_ID,HID_USAGE_Y);
    SetAxis(0x4000,DEV_ID,HID_USAGE_RY);
}

void PushAction(const InputAct& Act)
{
    {
        std::lock_guard<std::mutex> lock(inputQueMutex);
        inputQue.push(Act);
    }
    inputCV.notify_one();
}

void workThreadFunc()
{
    std::unique_lock<std::mutex> lock(inputQueMutex);
    while(inputThreadRunning)
    {
        inputCV.wait(lock, []{return !inputQue.empty()||!inputThreadRunning;});
        while(!inputQue.empty())
        {
            InputAct Act = inputQue.front();
            inputQue.pop();
            lock.unlock();
            ActionProc(Act);
            lock.lock();
        }
    }
}

void ActionProc(InputAct Act)
{
    static std::unordered_map<int, int> ButtonMap = {
        {ID_BTN_X,1}, {ID_BTN_A,2},
        {ID_BTN_B,3}, {ID_BTN_Y,4},
        {ID_LT,5}, {ID_RT,6},
        {ID_BTN_LB,7}, {ID_BTN_RB,8},
        {ID_LX_LEFT,HID_USAGE_X},
        {ID_LX_RIGHT,HID_USAGE_X},
        {ID_LY_UP,HID_USAGE_Y},
        {ID_LY_DOWN,HID_USAGE_Y}
    };
    static std::unordered_map<int, int> ValueMap = {
        {ID_LX_LEFT,0x1},
        {ID_LX_RIGHT,0x7000},
        {ID_LY_UP,0x1},
        {ID_LY_DOWN,0x7000}
    };
    if(Act.type == InputActType::Button)
    {
        SetBtn(Act.isPressed?TRUE:FALSE, DEV_ID, ButtonMap[Act.butID]);
#ifndef NDEBUG
        LogMessage("Button %d is %s.", ButtonMap[Act.butID], Act.isPressed?"pressed":"released");
#endif

    }
    else if(Act.type == InputActType::AxisMove)
    {
        SetAxis(Act.isPressed?ValueMap[Act.butID]:0x4000, DEV_ID, ButtonMap[Act.butID]);
#ifndef NDEBUG
        LogMessage("Axis %s is %s.", ButtonMap[Act.butID]==HID_USAGE_X?"X":"Y", Act.isPressed?"moving":"stopped");
#endif
    }

}
#pragma endregion



#pragma region GUI

//日志线程函数
void LogThread()
{
    while(logThreadRunning)
    {
        std::unique_lock<std::mutex> lock(logMutex);
        logCondition.wait(lock, []{return !logBuff.empty()||!logThreadRunning;});

        while(!logBuff.empty())
        {
            std::string LogMsg = logBuff.front();
            logBuff.pop();
            lock.unlock();
            
            int len = GetWindowTextLength(hLogBox);
            SendMessage(hLogBox, EM_SETSEL, (WPARAM)len, (LPARAM)len);
            SendMessage(hLogBox, EM_REPLACESEL, 0, (LPARAM)LogMsg.c_str());
            SendMessage(hLogBox, EM_REPLACESEL, 0, (LPARAM)_T("\r\n"));
            SendMessage(hLogBox, WM_VSCROLL, SB_BOTTOM, 0);
            lock.lock();
        }
    }
}

//输出日志
void LogMessage(const char* format, ...)
{
    if(!hLogBox) return ;
    char buffer[1024];
    va_list args;
    va_start(args,format);
    _vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args); 
    va_end(args);
    
    {
        std::lock_guard<std::mutex> lock(logMutex);
        std::string buffstr(buffer);
        logBuff.push(buffstr);
    }
    logCondition.notify_one();

}

static std::unordered_map<int,bool> Btnstate;//记录按钮状态

//摇杆按钮过程
LRESULT CALLBACK AxisButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int buttonID=GetDlgCtrlID(hwnd);
    WNDPROC oldProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch(msg)
    {
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONDOWN:
        {
            if(DEV_USABLE)
            {
                PushAction({buttonID, TRUE, InputActType::AxisMove});
            }
        }
        break;
        case WM_LBUTTONUP:
        {
            if(DEV_USABLE)
            {
                PushAction({buttonID, FALSE, InputActType::AxisMove});
            }
        }
        break;
    }
    return CallWindowProc(oldProc, hwnd, msg, wParam, lParam);
}

//触发按钮过程
LRESULT CALLBACK presButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int buttonID=GetDlgCtrlID(hwnd);
    WNDPROC oldProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg)
    {
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONDOWN:
        {
            if(DEV_USABLE) 
            {
                PushAction({buttonID, TRUE, InputActType::Button});
            }
        }
        break;
        case WM_LBUTTONUP:
        {
            if(DEV_USABLE)
            {
                PushAction({buttonID, FALSE, InputActType::Button});
            }
        }
        break;
    }
    return CallWindowProc(oldProc, hwnd, msg, wParam, lParam);
}

//设置子类化
void Subclassing(HWND hwnd, WNDPROC tarProc)
{
#ifndef NDEBUG
    LogMessage("Subclassing button %d.",GetDlgCtrlID(hwnd));                        //test
#endif
    WNDPROC oldProc = (WNDPROC)GetWindowLongPtr(hwnd,GWLP_WNDPROC);
    if(!oldProc)
    {
#ifndef NDEBUG
        LogMessage("oldProc is NULL!");                                             //test
#endif
        oldProc = DefWindowProc;
    }
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)oldProc);
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)tarProc);
#ifndef NDEBUG
    LogMessage("Subclassed: hwnd=%p, oldProc=%p, tarProc=%p",hwnd, oldProc, tarProc);//test
#endif
}

//封装按钮创建
HWND CreateButton(HWND hwnd, const TCHAR* text, int x,int y,int id)
{
    HWND rwnd;
    rwnd=CreateWindow(_T("BUTTON"), text, WS_VISIBLE | WS_CHILD, x, y, 60, 30, hwnd, (HMENU)id, NULL, NULL);
    return rwnd;
}

//创建 GUI
void CreateGUI(HWND hwnd)
{
    hLogBox = CreateWindow(_T("EDIT"), _T(""),
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        20, 200, 400, 200, hwnd, (HMENU)ID_LOG_BOX, NULL, NULL);
    SendMessage(hLogBox, EM_SETREADONLY, TRUE, 0);

    HWND hAxisLt = CreateButton(hwnd, _T("<"), 50, 120, ID_LX_LEFT);
    HWND hAxisRt = CreateButton(hwnd, _T(">"), 130, 120, ID_LX_RIGHT);
    HWND hAxisUp = CreateButton(hwnd, _T("^"), 90, 90, ID_LY_UP);
    HWND hAxisDn = CreateButton(hwnd, _T("v"), 90, 150, ID_LY_DOWN);
    
    HWND hButLT = CreateButton(hwnd, _T("LT"), 130, 50, ID_LT);
    HWND hButRT = CreateButton(hwnd, _T("RT"), 230, 50, ID_RT);
    HWND hButLB = CreateButton(hwnd, _T("LB"), 50, 50, ID_BTN_LB);
    HWND hButRB = CreateButton(hwnd, _T("RB"), 310, 50, ID_BTN_RB);
    
    int kpos=20;
    HWND hButA = CreateButton(hwnd, _T("A"), 250+kpos, 150, ID_BTN_A);
    HWND hButB = CreateButton(hwnd, _T("B"), 310+kpos, 120, ID_BTN_B);
    HWND hButX = CreateButton(hwnd, _T("X"), 190+kpos, 120, ID_BTN_X);
    HWND hButY = CreateButton(hwnd, _T("Y"), 250+kpos, 90, ID_BTN_Y);

    Subclassing(hAxisUp, AxisButtonProc);
    Subclassing(hAxisDn, AxisButtonProc);
    Subclassing(hAxisLt, AxisButtonProc);
    Subclassing(hAxisRt, AxisButtonProc);
    Subclassing(hButA, presButtonProc);
    Subclassing(hButB, presButtonProc);
    Subclassing(hButX, presButtonProc);
    Subclassing(hButY, presButtonProc);
    Subclassing(hButLB, presButtonProc);
    Subclassing(hButLT, presButtonProc);
    Subclassing(hButRB, presButtonProc);
    Subclassing(hButRT, presButtonProc);
    LogMessage("Hello!");
}

#pragma endregion

#pragma region Main
//主窗口过程
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM LParam)
{
    switch (msg)
    {
    case WM_CREATE:
        {
            CreateGUI(hwnd);
            int state = CheckVJD(DEV_ID);
            if(state<=0) 
            {
                LogMessage("Initializing error.");
                DEV_USABLE = 0;
            }
        }
        break;
    case WM_DESTROY:
        {
            LogMessage("Executing...");
            ResettVJD(DEV_ID);
            RelinquishVJD(DEV_ID);
            PostQuitMessage(0);
        }
        break;
    case WM_GETMINMAXINFO:
        {
            MINMAXINFO* pMinMax = (MINMAXINFO*) LParam;
            pMinMax->ptMaxTrackSize.x = 450;
            pMinMax->ptMaxTrackSize.y = 450;
            pMinMax->ptMaxSize.x = 450;
            pMinMax->ptMaxSize.y = 450;
            pMinMax->ptMinTrackSize.x = 450;
            pMinMax->ptMinTrackSize.y = 450;
        }
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, LParam);
        break;
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdshow)
{
    WNDCLASS wc={0};
    wc.lpfnWndProc=WndProc;
    wc.hInstance=hInstance;
    wc.lpszClassName=_T("vJoy GUI");
    RegisterClass(&wc);

    HWND hwnd = CreateWindow(wc.lpszClassName, _T("vJoy Contorller"),
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            100, 100, 450, 450, NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd,nCmdshow);
    UpdateWindow(hwnd);
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    logThreadRunning=TRUE;
    std::thread logger(LogThread);
    inputThreadRunning=TRUE;
    std::thread worker(workThreadFunc);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if(logThreadRunning)
    {
        logThreadRunning=FALSE;
        logCondition.notify_one();
        if(logger.joinable())
        {
            logger.join();
        }
    }
    if(inputThreadRunning)
    {
        inputThreadRunning=FALSE;
        inputCV.notify_one();
        if(worker.joinable())
        {
            worker.join();
        }
    }
    return msg.wParam;
}
#pragma endregion
