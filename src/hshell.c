#include "vt100.h"

#include <string.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include <pwd.h>

int g_ui_nextfreeline = 1;

char *GetNextRecycleBuffer()
{
#define GLOBAL_BUFFERS (12)
#define GLOBAL_BUFFER_LEN (1024)

    static char g_buffers[GLOBAL_BUFFERS][GLOBAL_BUFFER_LEN] = {0};
    static int g_buff_idx = 0;

    char *r = g_buffers[g_buff_idx];

    memset(r, 0, GLOBAL_BUFFER_LEN);

    g_buff_idx = (g_buff_idx + 1) % GLOBAL_BUFFERS;
    return r;

#undef GLOBAL_BUFFERS
}

static inline int IsInRange(int v, int min_inc, int max_inc) { return (v >= min_inc) && (v <= max_inc); }

/*
    Converts a /Full/Path/Into/ -> /F/P/Into/
    Converts a /ReallyLongPath/WithLogFolderNames/Into/ -> /RLP/WLFN/Into/
*/
char *MakePathShortForUser(char *path)
{
    char *buff = GetNextRecycleBuffer();

    return buff;
}

typedef char argv_t[10][255];
typedef char argv_s1_t[9][255];

void BuiltIn_ChangeDir(int argc, argv_s1_t argv)
{
    if (argc != 1)
        return; // TODO: Error, not enough args

    if (chdir(argv[1]))
    {
        perror("chdir");
    }
}

void BuiltIn_Help(int argc, argv_s1_t argv)
{
}

void BuiltIn_Exit(int argc, argv_s1_t argv)
{
    cleanup();
    exit(0);
}

typedef struct I_BuiltInCommand
{
    const char *cmd;
    int requiredArgs;
    void (*method)(int argc, argv_s1_t argv);
} I_BuiltInCommand;

I_BuiltInCommand g_builtins[] = {
    {.cmd = "cd", .requiredArgs = 1, .method = BuiltIn_ChangeDir},
    {.cmd = "help", .requiredArgs = 0, .method = BuiltIn_Help},
    {.cmd = "exit", .requiredArgs = 0, .method = BuiltIn_Exit},
};

I_BuiltInCommand *is_builtin(const char *cmd)
{
    for (int i = 0; i < sizeof(g_builtins) / sizeof(g_builtins[0]); i++)
    {
        if (strcmp(g_builtins[i].cmd, cmd) == 0)
        {
            return &g_builtins[i];
        }
    }

    return NULL;
}

char *DoUserInputRead()
{
    char *buffer = GetNextRecycleBuffer();
    int bi = 0;

    ShowCurrsor();
    WriteGraphicsOut();

    for (;;)
    {
        TERM_KEY tk = ReadKey();
        char lillbuf[4] = {0};

        if (tk == TK_ENTER && bi != 0)
            goto DONE_READING;

        if (IsInRange(tk, ' ', '~'))
        {
            lillbuf[0] = tk;
            lillbuf[1] = '\0';

            DrawText(lillbuf);
            WriteGraphicsOut();

            buffer[bi] = tk;
            bi += 1;
        }

        if (tk == TK_BACKSPACE && bi > 0)
        {
            lillbuf[0] = '\b';
            lillbuf[1] = ' ';
            lillbuf[2] = '\b';
            lillbuf[3] = '\0';

            buffer[bi] = '\0';
            bi -= 1;
            buffer[bi] = '\0';

            if (0 > bi)
                bi = 0;

            DrawText(lillbuf);
            WriteGraphicsOut();
        }
    }

DONE_READING:

    return buffer;
}

void LogToConsole(char *str)
{
    int r, c;
    GetScreenSize(&c, &r);

    SetCurrsorPos(0, g_ui_nextfreeline);
    DrawText(str);
    DrawText("\r\n");

    g_ui_nextfreeline += 1;

    WriteGraphicsOut();
}

typedef struct CmdSlice
{
    char *start;
    int len;

    int isPipeIntoNext;

} CmdSlice;

typedef struct CmdList
{
    CmdSlice command[10];
    int commands_used;
} CmdList;

const char *GetHomeDir()
{
    const char *homedir;

    if ((homedir = getenv("HOME")) == NULL)
    {
        homedir = getpwuid(getuid())->pw_dir;
    }

    return homedir;
}

/*
   $ENVVAR ; | $(CMD TO RUN) ~ &

    > mkdir test; ./doSomething.sh; rmdir test;
    XPand{
        .command[0] = "mkdir test"
        .command[1] = "./doSomething.sh"
        .command[2] = "rmdir test"
    }


    > find | grep raylib
    XPand{
        .command[0] = "find"
        .command[0].isPipe = 1
        .command[1] = "grep raylib"
        .command[1].isPipe = 0
    }

    >cd ~
    XPand{
        .command[0] = "cd /home/dir"
    }

    >echo $PATH
    XPand{
        .command[0] = "echo /bin/;/usr/bin/;"
    }

    >cd /; find | grep "raylib.so"
    XPand{
        .command[0] = cd /
        .command[1] = find
        .command[1].isPipe = true;
        .command[2] = grep "raylib.so"
    }

    >find | grep "raylib.so"
    XPand{
        .command[0]=find,
        .command[0].ispipe = true;
        .command[1]=grep "raylib.so"
    }

*/
CmdList ExpandInput(char *str)
{
    CmdList r = {0};

    char *c = str;

    int cmdidx = -1; /* -1 so the first pass incs to 0 */

NEXT:
    cmdidx += 1;

    r.command[cmdidx].start = c;

    // TODO: somewhere in here is where we will modify str, and insert our $Expanded vars
    while (*c != ';' && *c != '|' && *c)
        c += 1;

    r.command[cmdidx].len = c - r.command[cmdidx].start;

    // if value at c != \0
    if (*c)
    {
        if (*c == '|')
            r.command[cmdidx].isPipeIntoNext = 1;

        while (*c == ' ' || *c == ';' || *c == '|')
            c += 1; // trim the start of the next string

        goto NEXT;
    }

    r.commands_used = cmdidx + 1; // idx -> count

    return r;
}

void SplitOnSpaces(
    char *str,
    int *argc,
    int buffers,
    int bufferlen,
    char argv[buffers][bufferlen],
    int strlen)
{
    int arg_idx = 0, char_idx = 0;

    char *curbuf = argv[arg_idx];

    for (int i = 0; i < strlen; i++)
    {
        if (char_idx > bufferlen)
        {
            *argc = 0;
            // TODO: Arg is longer then max length
            return;
        }

        if (str[i] == ' ')
        {
            arg_idx += 1;
            char_idx = 0;
            curbuf = argv[arg_idx];
            continue;
        }

        curbuf[char_idx] = str[i];
        char_idx += 1;
    }

    *argc = arg_idx;
}

void printargs(char **ntargs)
{
    char *c = ntargs[0];
    int ac = 0;

    while (c != NULL)
    {
        printf("%d: \'%s\'\r\n", ac, c);
        ac += 1;
        c = ntargs[ac];
    }
}

void Exec(int argc, int bufflen, char argv[argc][bufflen])
{

    pid_t pid = fork();
    if (pid == -1)
    {
        perror("fork");
        exit(1);
    }
    else if (pid == 0)
    {
        char **args = calloc(sizeof(char *), argc + 1);

        args[argc] = NULL;

        for (int i = 0; i < argc; i++)
        {
            args[i] = argv[i];
        }

        int exec = execvp(argv[0], args);

        free(args);

        if (0 > exec)
        {
            perror("execvp");
        }

        exit(0); /* we are the child exiting here ... */
    }
    else
    {
        wait(NULL); /* we are waiting for the kid to die */
    }
}

void ExecuteInput(char *exp, int len)
{
    int argc = 0;
    char argv[10][255] = {0};

    SplitOnSpaces(exp, &argc, 9, 255, argv, len);

    I_BuiltInCommand *builtin = is_builtin(argv[0]);

    if (builtin)
    {
        builtin->method(argc, argv);
    }
    else
    {
        int rowBeforeExec, colBeforeExec,
            rowAfterExec, colAfterExec;

        GetCursorPosition(&colBeforeExec, &rowBeforeExec);
        StoreTerminalConfig();
        SetSensableTerminal();
        Exec(argc, 255, argv);
        RestoreTermConfig();
        GetCursorPosition(&colAfterExec, &rowAfterExec);
        g_ui_nextfreeline = rowAfterExec;
    }
}

void DoPipedCommand(CmdSlice first, CmdSlice into)
{
    puts("TODO: Pipe Command");
    return;

    int first_argc, into_argc;
    char first_argv[10][255] = {0}, into_argv[10][255] = {0};

    SplitOnSpaces(first.start, &first_argc, 9, 255, first_argv, first.len);
    SplitOnSpaces(into.start, &into_argc, 9, 255, into_argv, into.len);

    int pipefd[2];
    pid_t p1, p2;

    if (0 > pipe(pipefd))
    {
        perror("pipe");
        return;
    }

    if (0 > (p1 = fork()))
    {
        perror("fork");
        return;
    }

    if (p1 == 0)
    {
        /* we are the fisrt command */

        exit(0); // this fork will always exit when done
    }

    if (0 > (p2 = fork()))
    {
        perror("fork");
        return;
    }

    if (p2 == 0)
    {
        /* we are the 2nd command running */
        exit(0);
    }

    // THIS IS STILL A WORK IN PROGRESS

}

void ExecuteCommandList(CmdList cl)
{
    int cmd = 0;

    while (cl.commands_used > cmd)
    {

        if (cl.command[cmd].isPipeIntoNext)
        {
            DoPipedCommand(cl.command[cmd], cl.command[cmd + 1]);

            cmd += 2; // use both commands
        }
        else
        {
            ExecuteInput(cl.command[cmd].start, cl.command[cmd].len);
            cmd += 1; // use a single command
        }
    }
}

void DrawConsoleLine()
{
    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)))
    {
        DrawText(cwd);
    }
    else
        perror("getcwd");

    DrawText(">");
}

int main(int argc, char *argv[])
{
    InitConsole();
    ClearConsole();
    HomeCurrsor();
    WriteGraphicsOut();

    while (1)
    {

        int cols, rows;
        GetScreenSize(&cols, &rows);
        SetCurrsorPos(0, rows - 1);
        ClearLine();

        DrawConsoleLine();

        char *userinput = DoUserInputRead();
        if (!userinput)
            continue; /* No INput */

        LogToConsole(userinput);

        // todo:     GetCursorPosition(&colBeforeExec, &rowBeforeExec);
        //  StoreTerminalConfig();
        //  SetSensableTerminal();
        //  this stuff should be done before executeCommandList, and resotred after

        CmdList expand = ExpandInput(userinput);
        ExecuteCommandList(expand);
    }
}