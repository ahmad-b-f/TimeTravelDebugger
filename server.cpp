// ======================= TIME-TRAVEL DEBUGGER - SERVER TEMPLATE =======================

// Pipeline this file implements, top to bottom:
//   0. Receive  -- stream the client's .trace bytes straight to source.bin on disk
//   1. Pass 0X0   -- validity check (FUNC/FUNC_END matching)
//   2. Pass 0X1   -- resolve(): copy EVERY source line into resolve.bin as [offset][size][string], then patch CALL targets.
//   3. Pass 0X2   -- execute resolve.bin: tokenize ONE line at a time, update the call stack, take a snapshot -> Timeline
//   4. Pass 0X3   -- serialize Timeline -> session.tdbg(header + snapshot records + dense index)

#include <iostream>
#include <string>
#include <cstdint>
#include <fstream>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
using namespace std;

const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2;
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024;
const int32_t IO_BUFFER_SIZE = 64 * 1024;
const int32_t SOCKET_TIMEOUT_SEC = 5;

template <typename T>
class Stack
{
    struct Node
    {
        T data;
        Node *next;
    };
    Node *top;
    int32_t count;

public:
    Stack()
    {
        top = nullptr;
        count = 0;
    }
    ~Stack()
    {
        while (top)
        {
            Node *temp = top;
            top = top->next;
            delete temp;
        }
    }
    bool push(const T &val)
    {
        if (count >= MAX_STACK_DEPTH)
        {
            return false;
        }
        Node *n = new Node;
        n->data = val;
        n->next = top;
        top = n;
        count++;
        return true;
    }
    T pop()
    {
        if (top == nullptr)
        {
            cout << "Stack Underflow!" << endl;
            return T();
        }
        Node *temp = top;
        T val = temp->data;
        top = top->next;
        delete temp;
        count--;
        return val;
    }
    T &peek()
    {
        return top->data;
    }
    bool isEmpty()
    {
        return count == 0;
    }
    int32_t depth()
    {
        return count;
    }
    int32_t snapshot_into(T out[], int32_t maxLen)
    {
        int32_t i = count - 1;
        Node *p = top;
        while (p && i >= 0)
        {
            if (i < maxLen)
                out[i] = p->data;
            i--;
            p = p->next;
        }
        return count < maxLen ? count : maxLen;
    }
};

struct Variable
{
    char name[32];
    int32_t value;
};
struct Frame
{
    char func_name[32];
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};
struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};

struct TimelineNode
{
    Snapshot *data;
    TimelineNode *next;
    TimelineNode *prev;
};
class Timeline
{
    TimelineNode *head, *tail;
    int32_t stepCount;

public:
    Timeline()
    {
        head = nullptr;
        tail = nullptr;
        stepCount = 0;
    }
    ~Timeline()
    {
        while (head)
        {
            TimelineNode *t = head;
            head = head->next;
            delete t->data;
            delete t;
        }
    }
    void record(Snapshot *s)
    {
        TimelineNode *n = new TimelineNode;
        n->data = s;
        n->next = nullptr;
        n->prev = tail;
        if (tail)
            tail->next = n;
        else
            head = n;
        tail = n;
        stepCount++;
    }
    TimelineNode *begin()
    {
        return head;
    }
    int32_t getStepCount()
    {
        return stepCount;
    }
};

struct TTDBHeader
{
    char magic[4];
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};
void writeHeader(FILE *f, const TTDBHeader &h)
{
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);
    fwrite(&h.stepCount, sizeof(int32_t), 1, f);
    fwrite(&h.indexOffset, sizeof(int64_t), 1, f);
}

struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin;
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField;
    string targetFuncName;
};

// PASS 0x0: READING source.bin + VALIDITY CHECK
bool readSourceLine(ifstream &in, string &out)
{
    string s;
    while (getline(in, s))
    {
        int32_t st = 0, ed = s.size();
        while (st < ed && isspace((unsigned char)s[st]))
        {
            st++;
        }
        while (ed > st && isspace((unsigned char)s[ed - 1]))
        {
            ed--;
        }
        if (st < ed)
        {
            out = s.substr(st, ed - st);
            return true;
        }
    }
    return false;
}
string firstWord(const string &line)
{
    int32_t i = 0, n = line.size();
    while (i < n && line[i] == ' ')
    {
        i++;
    }
    int32_t k = i;
    while (k < n && line[k] != ' ')
    {
        k++;
    }
    return line.substr(i, k - i);
}
string secondWord(const string &line)
{
    int32_t i = 0, n = line.size();
    while (i < n && line[i] == ' ')
    {
        i++;
    }
    while (i < n && line[i] != ' ')
    {
        i++;
    }
    while (i < n && line[i] == ' ')
    {
        i++;
    }
    int32_t j = i;
    while (j < n && line[j] != ' ')
    {
        j++;
    }
    return line.substr(i, j - i);
}
bool validateProgram(const char *sourcePath)
{
    ifstream in(sourcePath);
    if (!in)
    {
        cout << "error: cannot open source" << endl;
        return false;
    }
    string line;
    bool inside_function = false;
    int32_t ln = 0;
    while (readSourceLine(in, line))
    {
        ln++;
        string w = firstWord(line);
        if (w == "func")
        {
            if (inside_function)
            {
                cout << "error: nested func at line " << ln << endl;
                return false;
            }
            inside_function = true;
        }
        else if (w == "func_end")
        {
            if (!inside_function)
            {
                cout << "error: func_end without func at line " << ln << endl;
                return false;
            }
            inside_function = false;
        }
    }
    if (inside_function)
    {
        cout << "error: func without func_end" << endl;
        return false;
    }
    return true;
}

// PASS 0x1: RESOLVE() -> resolve.bin
int64_t writeResolveRecord(FILE *f, int64_t offsetField, const string &text)
{
    int64_t pos = ftell(f);
    int32_t size = text.size();
    fwrite(&offsetField, 8, 1, f);
    fwrite(&size, 4, 1, f);
    fwrite(text.c_str(), 1, size, f);
    return pos;
}

int64_t readResolveRecord(FILE *f, int64_t &field, string &outText)
{
    int64_t pos = ftell(f);
    int32_t size;
    if (fread(&field, 8, 1, f) != 1)
        return -1;
    if (fread(&size, 4, 1, f) != 1)
        return -1;
    if (size < 0 || size > 100000)
        return -1;
    outText.resize(size);
    if (size > 0 && fread(&outText[0], 1, size, f) != (size_t)size)
        return -1;
    return pos;
}

int64_t resolveProgram(const char *sourcePath, const char *resolveBinPath)
{
    FuncEntry funcArray[MAX_FUNCS];
    int32_t funcCount = 0;

    PendingPatch patches[MAX_PATCHES];
    int32_t patchCount = 0;

    ifstream in(sourcePath);

    if (!in)
    {
        cout << "error: cannot open source file!" << endl;
        return -1;
    }
    FILE *f = fopen(resolveBinPath, "wb+");
    if (!f)
    {
        cout << "error: cannot create resolve.bin" << endl;
        return -1;
    }
    string line;
    bool ok = true;

    while (ok && readSourceLine(in, line))
    {
        string w = firstWord(line);

        int64_t field = ftell(f);

        if (w == "call")
            field = 0;

        int64_t pos = writeResolveRecord(f, field, line);

        if (w == "func")
        {
            string functionName = secondWord(line);

            if (funcCount >= MAX_FUNCS)
            {
                cout << "too many functions" << endl;
                ok = false;
                break;
            }

            bool alreadyExists = false;

            for (int32_t i = 0; i < funcCount; i++)
            {
                if (funcArray[i].funcName == functionName)
                {
                    alreadyExists = true;
                    break;
                }
            }

            if (alreadyExists)
            {
                cout << "duplicate function " << functionName << endl;
                ok = false;
                break;
            }

            funcArray[funcCount].funcName = functionName;
            funcArray[funcCount].byteOffsetInResolveBin = pos;
            funcCount++;
        }
        else if (w == "call")
        {
            if (patchCount >= MAX_PATCHES)
            {
                cout << "too many calls" << endl;
                ok = false;
                break;
            }

            patches[patchCount].byteOffsetOfOffsetField = pos;
            patches[patchCount].targetFuncName = secondWord(line);
            patchCount++;
        }
    }
    in.close();
    int64_t mainOffset = -1;
    if (ok)
    {
        for (int32_t i = 0; i < patchCount; i++)
        {
            string targetFunctionName = patches[i].targetFuncName;
            int64_t targetOffset = -1;

            for (int32_t j = 0; j < funcCount; j++)
            {
                if (funcArray[j].funcName == targetFunctionName)
                {
                    targetOffset = funcArray[j].byteOffsetInResolveBin;
                    break;
                }
            }

            if (targetOffset == -1)
            {
                cout << "call to undefined function " << targetFunctionName << endl;
                ok = false;
                break;
            }

            fseek(f, patches[i].byteOffsetOfOffsetField, SEEK_SET);
            fwrite(&targetOffset, 8, 1, f);
        }
    }
    if (ok)
    {
        for (int32_t i = 0; i < funcCount; i++)
        {
            if (funcArray[i].funcName == "main")
            {
                mainOffset = funcArray[i].byteOffsetInResolveBin;
                break;
            }
        }

        if (mainOffset == -1)
            cout << "main function not found" << endl;
    }

    fclose(f);

    return mainOffset;
}

// PASS 0x2: EXECUTION (tokenization happens here)
enum TokenType
{
    KEYWORD,
    IDENTIFIER,
    PARAM
};
struct Token
{
    TokenType type;
    string text;
};
int32_t tokenizeLine(const string &line, Token tokens[], int32_t maxTokens)
{
    int32_t n = 0, i = 0, len = line.size();
    while (i < len && n < maxTokens)
    {
        while (i < len && isspace((unsigned char)line[i]))
        {
            i++;
        }
        if (i >= len)
            break;
        int32_t j = i;
        while (j < len && !isspace((unsigned char)line[j]))
        {
            j++;
        }
        tokens[n].text = line.substr(i, j - i);
        tokens[n].type = n == 0 ? KEYWORD : (n == 1 ? IDENTIFIER : PARAM);
        n++;
        i = j;
    }
    return n;
}
Snapshot *buildSnapshot(Stack<Frame> &callStack)
{
    Snapshot *s = new Snapshot;
    memset(s, 0, sizeof(Snapshot));
    s->stackDepth = callStack.snapshot_into(s->callStack, MAX_STACK_DEPTH);
    return s;
}
bool executeProgram(const char *resolveBinPath, int64_t mainOffset, Timeline &timeline)
{
    FILE *f = fopen(resolveBinPath, "rb");
    if (!f)
    {
        cout << "error: cannot open resolve.bin" << endl;
        return false;
    }
    Stack<Frame> callStack;
    static char ca[MAX_STACK_DEPTH][MAX_VARS_PER_FRAME][32];
    Token tk[MAX_TOKENS], ht[MAX_TOKENS];
    string line, hs;
    int64_t field, hf;

    fseek(f, mainOffset, SEEK_SET);
    if (readResolveRecord(f, field, line) < 0)
    {
        cout << "error: cannot read main" << endl;
        fclose(f);
        return false;
    }
    tokenizeLine(line, tk, MAX_TOKENS);
    Frame mf;
    memset(&mf, 0, sizeof(Frame));
    strncpy(mf.func_name, tk[1].text.c_str(), 31);
    mf.returnLine = -1;
    callStack.push(mf);
    int64_t pc = ftell(f);
    bool ok = true;

    while (ok && !callStack.isEmpty())
    {
        fseek(f, pc, SEEK_SET);
        int64_t at = readResolveRecord(f, field, line);
        if (at < 0)
        {
            cout << "error: ran past end of resolve.bin" << endl;
            ok = false;
            break;
        }
        int64_t nx = ftell(f);
        int32_t n = tokenizeLine(line, tk, MAX_TOKENS);
        Frame &cur = callStack.peek();
        const string &k = tk[0].text;
        const char *er = nullptr;

        if (k == "set" || k == "add" || k == "sub" || k == "mul" || k == "div")
        {
            if (n < 3)
                er = "missing operand";
            else
            {
                Variable *p = nullptr;
                int32_t v = 0;
                bool got = false;
                const string &s = tk[2].text;
                for (int32_t i = 0; i < cur.argc + cur.localCount && !p; i++)
                {
                    Variable *c = i < cur.argc ? &cur.argv[i] : &cur.locals[i - cur.argc];
                    if (tk[1].text == c->name)
                        p = c;
                }
                if ((s[0] >= '0' && s[0] <= '9') || (s[0] == '-' && s.size() > 1))
                {
                    v = atoi(s.c_str());
                    got = true;
                }
                for (int32_t i = 0; i < cur.argc + cur.localCount && !got; i++)
                {
                    Variable *c = i < cur.argc ? &cur.argv[i] : &cur.locals[i - cur.argc];
                    if (s == c->name)
                    {
                        v = c->value;
                        got = true;
                    }
                }
                if (!got)
                    er = "undefined variable";
                else if (k == "set")
                {
                    if (!p)
                    {
                        if (cur.localCount >= MAX_VARS_PER_FRAME)
                            er = "too many variables";
                        else
                        {
                            p = &cur.locals[cur.localCount++];
                            strncpy(p->name, tk[1].text.c_str(), 31);
                        }
                    }
                    if (p)
                        p->value = v;
                }
                else if (!p)
                    er = "undefined variable";
                else if (k == "div" && v == 0)
                    er = "division by zero";
                else
                {
                    switch (k[0])
                    {
                    case 'a':
                        p->value += v;
                        break;
                    case 's':
                        p->value -= v;
                        break;
                    case 'm':
                        p->value *= v;
                        break;
                    case 'd':
                        p->value /= v;
                        break;
                    }
                }
            }
            pc = nx;
        }
        else if (k == "call")
        {
            int32_t ac = n - 2;
            int32_t hn = 0;
            int64_t after = -1;
            fseek(f, field, SEEK_SET);
            if (readResolveRecord(f, hf, hs) >= 0)
            {
                after = ftell(f);
                hn = tokenizeLine(hs, ht, MAX_TOKENS);
            }
            if (hn < 2 || ht[0].text != "func")
                er = "bad call target";
            else if (hn - 2 != ac)
                er = "argument count mismatch";
            else
            {
                int32_t d = callStack.depth();
                Frame nf;
                memset(&nf, 0, sizeof(Frame));
                strncpy(nf.func_name, ht[1].text.c_str(), 31);
                nf.argc = ac;
                nf.returnLine = (int32_t)nx;
                for (int32_t i = 0; i < ac && !er; i++)
                {
                    const string &s = tk[2 + i].text;
                    int32_t v = 0;
                    bool got = false;
                    if ((s[0] >= '0' && s[0] <= '9') || (s[0] == '-' && s.size() > 1))
                    {
                        v = atoi(s.c_str());
                        got = true;
                    }
                    for (int32_t j = 0; j < cur.argc + cur.localCount && !got; j++)
                    {
                        Variable *c = j < cur.argc ? &cur.argv[j] : &cur.locals[j - cur.argc];
                        if (s == c->name)
                        {
                            v = c->value;
                            got = true;
                        }
                    }
                    if (!got)
                        er = "undefined argument";
                    else
                    {
                        strncpy(nf.argv[i].name, ht[2 + i].text.c_str(), 31);
                        nf.argv[i].value = v;
                        strncpy(ca[d][i], s.c_str(), 31);
                    }
                }
                if (!er)
                {
                    if (!callStack.push(nf))
                        er = "stack overflow";
                    else
                        pc = after;
                }
            }
        }
        else if (k == "func_end")
        {
            int32_t d = callStack.depth() - 1;
            Frame done = callStack.pop();
            if (!callStack.isEmpty())
            {
                Frame &cl = callStack.peek();
                for (int32_t i = 0; i < done.argc; i++)
                {
                    for (int32_t j = 0; j < cl.argc + cl.localCount; j++)
                    {
                        Variable *c = j < cl.argc ? &cl.argv[j] : &cl.locals[j - cl.argc];
                        if (strcmp(ca[d][i], c->name) == 0)
                        {
                            c->value = done.argv[i].value;
                            break;
                        }
                    }
                }
            }
            pc = done.returnLine;
        }
        else
            er = "unknown instruction";

        if (er)
        {
            cout << "error: " << er << " at offset " << at << endl;
            ok = false;
            break;
        }
        timeline.record(buildSnapshot(callStack));
    }
    fclose(f);
    return ok;
}

// PASS 0x3: SERIALIZE TIMELINE
void writeTdbg(Timeline &timeline, const char *tdbgPath)
{
    FILE *f = fopen(tdbgPath, "wb");
    if (!f)
    {
        cout << "error: cannot create " << tdbgPath << endl;
        return;
    }
    TTDBHeader h;
    memcpy(h.magic, "TTDB", 4);
    h.version = 1;
    h.stepCount = timeline.getStepCount();
    h.indexOffset = 0;
    writeHeader(f, h);

    int64_t *idx = new int64_t[h.stepCount > 0 ? h.stepCount : 1];
    int32_t i = 0;
    for (TimelineNode *p = timeline.begin(); p; p = p->next)
    {
        idx[i++] = ftell(f);
        fwrite(&p->data->stackDepth, sizeof(int32_t), 1, f);
        fwrite(p->data->callStack, sizeof(Frame), p->data->stackDepth, f);
    }
    h.indexOffset = ftell(f);
    fwrite(idx, sizeof(int64_t), h.stepCount, f);

    fseek(f, 0, SEEK_SET);
    writeHeader(f, h);
    fclose(f);
    delete[] idx;
}

// PASS 0x4 (STAGE 0): RECEIVE FROM CLIENT / REPLY
bool readn(int s, char *b, uint64_t n)
{
    while (n > 0)
    {
        ssize_t r = recv(s, b, n, 0);
        if (r <= 0)
            return false;
        b += r;
        n -= r;
    }
    return true;
}
bool sendn(int s, const char *b, uint64_t n)
{
    while (n > 0)
    {
        ssize_t r = send(s, b, n, MSG_NOSIGNAL);
        if (r <= 0)
            return false;
        b += r;
        n -= r;
    }
    return true;
}
bool receiveSource(int c, const char *path)
{
    uint64_t len;
    if (!readn(c, (char *)&len, 8))
    {
        cout << "error: no length received from client" << endl;
        return false;
    }
    if (len == 0 || len > MAX_SOURCE_BYTES)
    {
        cout << "error: bad source size " << len << endl;
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        cout << "error: cannot create " << path << endl;
        return false;
    }
    char buf[IO_BUFFER_SIZE];
    while (len > 0)
    {
        uint64_t k = len < (uint64_t)IO_BUFFER_SIZE ? len : IO_BUFFER_SIZE;
        if (!readn(c, buf, k))
        {
            cout << "error: connection lost or timed out while receiving" << endl;
            fclose(f);
            return false;
        }
        fwrite(buf, 1, k, f);
        len -= k;
    }
    fclose(f);
    return true;
}
bool sendReply(int c, uint8_t status, const char *msg, uint64_t n)
{
    return sendn(c, (char *)&status, 1) && sendn(c, (char *)&n, 8) && sendn(c, msg, n);
}
bool sendTdbg(int c, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    uint64_t n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t st = 0;
    bool ok = sendn(c, (char *)&st, 1) && sendn(c, (char *)&n, 8);
    char buf[IO_BUFFER_SIZE];
    while (ok && n > 0)
    {
        uint64_t k = n < (uint64_t)IO_BUFFER_SIZE ? n : IO_BUFFER_SIZE;
        if (fread(buf, 1, k, f) != k)
            break;
        ok = sendn(c, buf, k);
        n -= k;
    }
    fclose(f);
    return ok && n == 0;
}
void handleClient(int c)
{
    timeval tv;
    tv.tv_sec = SOCKET_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    remove("session.tdbg");

    stringstream ss;
    streambuf *old = cout.rdbuf(ss.rdbuf());
    bool ok = receiveSource(c, "source.bin") && validateProgram("source.bin");
    if (ok)
    {
        int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");
        ok = mainOffset >= 0;
        if (ok)
        {
            Timeline timeline;
            ok = executeProgram("resolve.bin", mainOffset, timeline);
            if (ok)
                writeTdbg(timeline, "session.tdbg");
        }
    }
    cout.rdbuf(old);

    if (ok && sendTdbg(c, "session.tdbg"))
    {
        cout << "client served: session.tdbg sent" << endl;
        return;
    }
    string m = ss.str();
    if (m.empty())
        m = "error: server could not produce session.tdbg\n";
    cout << "client failed: " << m;
    sendReply(c, 1, m.c_str(), m.size());
}

// entry points used by main.cpp
int runFile()
{
    if (!validateProgram("source.bin"))
        return 1;
    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");
    if (mainOffset < 0)
        return 1;
    Timeline timeline;
    if (!executeProgram("resolve.bin", mainOffset, timeline))
        return 1;
    writeTdbg(timeline, "session.tdbg");
    return 0;
}
int runServer(int port)
{
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0)
    {
        cout << "error: cannot create socket" << endl;
        return 1;
    }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(srv, (sockaddr *)&a, sizeof(a)) < 0 || listen(srv, 5) < 0)
    {
        cout << "error: cannot listen on port " << port << endl;
        close(srv);
        return 1;
    }
    cout << "server listening on port " << port << endl;
    while (true)
    {
        int c = accept(srv, nullptr, nullptr);
        if (c < 0)
            continue;
        handleClient(c);
        close(c);
    }
    return 0;
}