#define VERSION_NAME "TermMarkV1"

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))

struct buffer
{
    int MaxCount;
    int Count;
    char *Data;
};

static char NumberTable[256][4];

static void AppendChar(buffer *Buffer, char Char)
{
    if(Buffer->Count < Buffer->MaxCount) Buffer->Data[Buffer->Count++] = Char;
}

static void AppendString(buffer *Buffer, char const *String)
{
    while(*String) AppendChar(Buffer, *String++);
}

static void AppendDecimal(buffer *Buffer, int Value)
{
    if(Value < 0)
    {
        AppendChar(Buffer, '-');
        Value = -Value;
    }
    
    int Remains = Value;
    for(int Divisor = 1000000000;
        Divisor > 0;
        Divisor /= 10)
    {
        int Digit = Remains / Divisor;
        Remains -= Digit*Divisor;
        
        if(Digit || (Value != Remains) || (Divisor == 1))
        {
            AppendChar(Buffer, (char)('0' + Digit));
        }
    }
}

static void AppendGoto(buffer *Buffer, int X, int Y)
{
    AppendString(Buffer, "\x1b[");
    AppendDecimal(Buffer, Y);
    AppendString(Buffer, ";");
    AppendDecimal(Buffer, X);
    AppendString(Buffer, "H");
}

static void AppendColor(buffer *Buffer, int IsForeground, int unsigned Red, int unsigned Green, int unsigned Blue)
{
    AppendString(Buffer, IsForeground ? "\x1b[38;2;" : "\x1b[48;2;");
    AppendString(Buffer, NumberTable[Red & 0xff]);
    AppendChar(Buffer, ';');
    AppendString(Buffer, NumberTable[Green & 0xff]);
    AppendChar(Buffer, ';');
    AppendString(Buffer, NumberTable[Blue & 0xff]);
    AppendChar(Buffer, 'm');
}

static int GetMS(int long long Start, int long long End, int long long Frequency)
{
    int Result = (int)(1000*(End - Start) / Frequency);
    return Result;
}

static void AppendStat(buffer *Buffer, char const *Name, int Value, char const *Suffix = "")
{
    AppendString(Buffer, Name);
    AppendString(Buffer, ": ");
    AppendDecimal(Buffer, Value);
    AppendString(Buffer, Suffix);
    AppendString(Buffer, "  ");
}

#define MAX_TERM_WIDTH 4096
#define MAX_TERM_HEIGHT 4096
static char TerminalBuffer[256+16*MAX_TERM_WIDTH*MAX_TERM_HEIGHT];

#if defined(_WIN64) || defined(_WIN32)
#define ISWINDOWS
#endif


#ifdef ISWINDOWS
    #include <windows.h>
    #include <intrin.h>

    #define DO_WRITE( Terminal, Data, Count ) \
         WriteConsoleA(Terminal, Data, Count, 0, 0)
#else
    #include <sys/types.h>
    #include <sys/ioctl.h>
    #include <sys/select.h>
    #include <unistd.h>
    #include <termios.h>
    #include <poll.h>
    #include <time.h>

    #define DO_WRITE( Terminal, Data, Count ) \
         write(Terminal, Data, Count )

    static int GetMS( timespec Start, timespec End )
    {
      timespec delta;
      if ( End.tv_nsec - Start.tv_nsec < 0 ) {
        delta.tv_sec = End.tv_sec - Start.tv_sec - 1 ;
        delta.tv_nsec = 1000000000 + End.tv_sec - Start.tv_nsec;
      } else {
        delta.tv_sec = End.tv_sec - Start.tv_sec;
        delta.tv_nsec = End.tv_nsec - Start.tv_nsec;
      }

      return int( delta.tv_sec * 1000.0 + delta.tv_nsec / ( 1000.0 * 1000.0 ) );
    }

    timespec QueryMonotonicClock()
    {
      timespec t = {};
      clock_gettime( CLOCK_MONOTONIC_RAW, &t );
      return t;
    }

#endif


#ifdef ISWINDOWS
extern "C" void mainCRTStartup(void)
{
#else
int main()
{
#endif
#ifdef ISWINDOWS
    char CPU[65] = {};
    for(int SegmentIndex = 0;
        SegmentIndex < 3;
        ++SegmentIndex)
    {
        __cpuid((int *)(CPU + 16*SegmentIndex), 0x80000002 + SegmentIndex);
    }
#endif
    
    for(int Num = 0; Num < 256; ++Num)
    {
        buffer NumBuf = {sizeof(NumberTable[Num]), 0, NumberTable[Num]};
        AppendDecimal(&NumBuf, Num);
        AppendChar(&NumBuf, 0);
    }
    
#ifdef ISWINDOWS
    #define TIMEPOINT( Name ) \
        LARGE_INTEGER Name; \
        QueryPerformanceCounter( & Name )

    HANDLE TerminalIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE TerminalOut = GetStdHandle(STD_OUTPUT_HANDLE);
    
    DWORD WinConMode = 0;
    DWORD EnableVirtualTerminalProcessing = 0x0004;
    int VirtualTerminalSupport = (GetConsoleMode(TerminalOut, &WinConMode) &&
                                  SetConsoleMode(TerminalOut, (WinConMode & ~(ENABLE_ECHO_INPUT|ENABLE_LINE_INPUT)) |
                                                 EnableVirtualTerminalProcessing));
    
    LARGE_INTEGER Freq;
    QueryPerformanceFrequency(&Freq);
#else
    #define TIMEPOINT( Name ) auto Name = QueryMonotonicClock()
    // Put the terminal in raw mode to handle input
    termios orig_termios;
    tcgetattr( STDIN_FILENO, &orig_termios );
    termios raw = orig_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    tcsetattr( STDIN_FILENO, TCSAFLUSH, &raw );

    pollfd poll_in = {};
    poll_in.fd = STDIN_FILENO;
    poll_in.events = POLLIN;
    poll_in.revents = POLLIN;

    auto TerminalIn = STDIN_FILENO;
    auto TerminalOut = STDOUT_FILENO;
#endif
    
    int PrepMS = 0;
    int WriteMS = 0;
    int ReadMS = 0;
    int TotalMS = 0;
    
    int Running = true;
    int FrameIndex = 0;
    int DimIsKnown = false;
    
    int WritePerLine = false;
    int ColorPerFrame = false;
    
    int Width = 0;
    int Height = 0;
    
    int ByteCount = 0;
    int TermMark = 0;
    int StatPercent = 0;
    
    int long long TermMarkAccum = 0;
#ifdef ISWINDOWS
    LARGE_INTEGER AverageMark = {};
#else
    timespec AverageMark = {};
#endif

    // clear the screen
    DO_WRITE(TerminalOut, "\x1b[2J", 4);
    
    while(Running)
    {
        int NextByteCount = 0;
        
        TIMEPOINT(A);
        
        if(TermMarkAccum == 0)
        {
            AverageMark = A;
        }
        else
        {
#ifdef ISWINDOWS
            int long long AvgMS = 1000*(A.QuadPart - AverageMark.QuadPart) / Freq.QuadPart;
#else
            auto AvgMS = GetMS( AverageMark, A );
#endif
            int long long StatMS = 10000;
            if(AvgMS > StatMS)
            {
                TermMark = (int)(1000*(TermMarkAccum / 1024) / AvgMS);
                AverageMark = A;
                TermMarkAccum = 0;
            }
            
            StatPercent = (int)(100*AvgMS / StatMS);
        }
        
        if(!DimIsKnown)
        {
#ifdef ISWINDOWS
            CONSOLE_SCREEN_BUFFER_INFO ConsoleInfo;
            GetConsoleScreenBufferInfo(TerminalOut, &ConsoleInfo);
            Width = ConsoleInfo.srWindow.Right - ConsoleInfo.srWindow.Left;
            Height = ConsoleInfo.srWindow.Bottom - ConsoleInfo.srWindow.Top;
            DimIsKnown = true;
#else
            winsize sz;
            ioctl(STDOUT_FILENO, TIOCGWINSZ, &sz);
            Width = sz.ws_col;
            Height = sz.ws_row;
            DimIsKnown = true;
#endif
        }
        
        if(Width > MAX_TERM_WIDTH) Width = MAX_TERM_WIDTH;
        if(Height > MAX_TERM_HEIGHT) Height = MAX_TERM_HEIGHT;

        auto DrawHeight = Height - 3;
        
        buffer Frame = {sizeof(TerminalBuffer), 0, TerminalBuffer};

        for(int Y = 3; Y <= Height; ++Y)
        {
            AppendGoto(&Frame, 1, 1 + Y);
            for(int X = 0; X < Width; ++X)
            {
                if(!ColorPerFrame)
                {
                    int BackRed = FrameIndex + Y + X;
                    int BackGreen = FrameIndex + Y;
                    int BackBlue = FrameIndex;
                    
                    int ForeRed = FrameIndex;
                    int ForeGreen = FrameIndex + Y;
                    int ForeBlue = FrameIndex + Y + X;
                    
                    AppendColor(&Frame, false, BackRed, BackGreen, BackBlue);
                    AppendColor(&Frame, true, ForeRed, ForeGreen, ForeBlue);
                }
                
                char Char = 'a' + (char)((FrameIndex + X + Y) % ('z' - 'a'));
                AppendChar(&Frame, Char);
            }
        
            if(WritePerLine)
            {
                NextByteCount += Frame.Count;
                DO_WRITE(TerminalOut, Frame.Data, Frame.Count);
                Frame.Count = 0;
            }
        }
        
        AppendColor(&Frame, false, 0, 0, 0);
        AppendColor(&Frame, true, 255, 255, 255);
        AppendGoto(&Frame, 1, 1);
        AppendStat(&Frame, "Glyphs", (Width*DrawHeight) / 1024, "k");
        AppendStat(&Frame, "Bytes", ByteCount / 1024, "kb");
        AppendStat(&Frame, "Frame", FrameIndex);

        if(!WritePerLine) AppendStat(&Frame, "Prep", PrepMS, "ms");
        AppendStat(&Frame, "Write", WriteMS, "ms");
        AppendStat(&Frame, "Read", ReadMS, "ms");
        AppendStat(&Frame, "Total", TotalMS, "ms");
        
        AppendGoto(&Frame, 1, 2);
#ifdef ISWINDOWS
        AppendString(&Frame, WritePerLine ? "[F1]:write per line " : "[F1]:write per frame ");
        AppendString(&Frame, ColorPerFrame ? "[F2]:color per frame " : "[F2]:color per char ");
#else
        AppendString(&Frame, WritePerLine ? "[L]:write per line " : "[L]:write per frame ");
        AppendString(&Frame, ColorPerFrame ? "[C]:color per frame " : "[C]:color per char ");
#endif
        
        if(!WritePerLine)
        {
            AppendGoto(&Frame, 1, 3);
            if(TermMark)
            {
                AppendStat(&Frame, VERSION_NAME, TermMark, ColorPerFrame ? "kg/s" : "kcg/s");
#ifdef ISWINDOWS
                AppendString(&Frame, "(");
                AppendString(&Frame, CPU);
                AppendString(&Frame, " Win32 ");
                AppendString(&Frame, VirtualTerminalSupport ? "VTS)" : "NO VTS REPORTED)");
#else
                AppendString(&Frame, "(POSIX)");
#endif
            }
            else
            {
                AppendStat(&Frame, "(collecting", StatPercent, "%)");
            }
        }
        
        TIMEPOINT( B );
        
        NextByteCount += Frame.Count;
        DO_WRITE(TerminalOut, Frame.Data, Frame.Count);
        
        TIMEPOINT( C );

        int ResetStats = false;
#ifdef ISWINDOWS
        while(WaitForSingleObject(TerminalIn, 0) == WAIT_OBJECT_0)
        {
            INPUT_RECORD Record;
            DWORD RecordCount = 0;
            ReadConsoleInput(TerminalIn, &Record, 1, &RecordCount);
            if(RecordCount)
            {
                if((Record.EventType == KEY_EVENT) &&
                   (Record.Event.KeyEvent.bKeyDown) &&
                   (Record.Event.KeyEvent.wRepeatCount == 1))
                {
                    switch(Record.Event.KeyEvent.wVirtualKeyCode)
                    {
                        case VK_ESCAPE: Running = false; break;
                        case VK_F1:
                        {
                            WritePerLine = !WritePerLine;
                            ResetStats = true;
                        } break;
                        
                        case VK_F2:
                        {
                            ColorPerFrame = !ColorPerFrame;
                            ResetStats = true;
                        } break;
                    }
                }
                else if(Record.EventType == WINDOW_BUFFER_SIZE_EVENT)
                {
                    DimIsKnown = false;
                    ResetStats = true;
                }
            }
        }
#else
        while ( true )
        {
            auto s = poll( &poll_in, 1, 0 );
            if ( s < 0 )
            {
                return -1;
            }
            else if ( s == 0 )
            {
                break;
            }
            else
            {
                char ch = '\0';
                if ( read( STDIN_FILENO, &ch, 1 ) < 1 || ch == 'q' )
                {
                    Running = false;
                    break;
                }
                // CBA to parse term codes for f-keys and stuff
                else if ( ch == 'L' )
                {
                    WritePerLine = !WritePerLine;
                    ResetStats = true;
                }
                else if ( ch == 'C' )
                {
                    ColorPerFrame = !ColorPerFrame;
                    ResetStats = true;
                }
            }

            // TODO : detecting terminal resize on posix
        }
#endif
        
        TIMEPOINT( D );
        
#ifdef ISWINDOWS
        PrepMS = GetMS(A.QuadPart, B.QuadPart, Freq.QuadPart);
        WriteMS = GetMS(B.QuadPart, C.QuadPart, Freq.QuadPart);
        ReadMS = GetMS(C.QuadPart, D.QuadPart, Freq.QuadPart);
        TotalMS = GetMS(A.QuadPart, D.QuadPart, Freq.QuadPart);
#else
        PrepMS = GetMS(A, B);
        WriteMS = GetMS(B, C);
        ReadMS = GetMS(C,D);
        TotalMS = GetMS(A,D);
#endif
        ByteCount = NextByteCount;
        ++FrameIndex;
        
        if(ResetStats)
        {
            TermMarkAccum = TermMark = 0;
        }
        else
        {
            TermMarkAccum += Width*DrawHeight;
        }
    }

#ifndef ISWINDOWS
    tcsetattr( STDIN_FILENO, TCSAFLUSH, &orig_termios );
#endif
}

//
// NOTE(casey): Support definitions for CRT-less Visual Studio and CLANG
//

#ifdef ISWINDOWS

#ifndef __clang__
#undef function
#pragma function(memset)
#endif
extern "C" void *memset(void *DestInit, int Source, size_t Size)
{
    unsigned char *Dest = (unsigned char *)DestInit;
    while(Size--) *Dest++ = (unsigned char)Source;
    
    return(DestInit);
}
    
#ifndef __clang__
#pragma function(memcpy)
#endif
extern "C" void *memcpy(void *DestInit, void const *SourceInit, size_t Size)
{
    unsigned char *Source = (unsigned char *)SourceInit;
    unsigned char *Dest = (unsigned char *)DestInit;
    while(Size--) *Dest++ = *Source++;
    
    return(DestInit);
}

#endif
