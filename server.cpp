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
#include <cstdint>
#include <cstdio>
using namespace std;

// ---- Constants ----
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2; // kW + func_name + upto 16 params/args
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024; // sanity cap on the declared file length
const int32_t IO_BUFFER_SIZE = 64 * 1024;                  // fixed buffer for streaming to/from disk
const int32_t SOCKET_TIMEOUT_SEC = 5;                      // TODO: apply as SO_RCVTIMEO so a deadclient can't hang the server forever

// ---- Custom data structures

// Stack: back the live Call Stack during execution
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
    // Implement these functions:
    Stack()
    { 
        top = nullptr;
        count = 0;
    }
    ~Stack()
    {
        while(top)
        {
            Node* temp = top;
            top = top->next;
            delete temp;
        }
    }
        void push(const T &val)
    {
        if(count >= MAX_STACK_DEPTH)
        {
            cout<<"Stack Overflow!"<<endl;
            rteurn;
        }
        Node* n = new Node;
        n->data = val;
        n->next = top;
        top = n;
        count++;
    }
    T pop()
    {
        if(top == nullptr)
        {
            cout<<"Stack Underflow!"<<endl;
            retrurn;
            Node* temp =top;
            T val = temp->data;
            top = top->next;
            delete temp;
            count--;
            return val;
        }
    }
    T &peek()
    {
        return top->data;
    }
    bool isEmpty()
    {
        rturn count == 0;
    }
    int32_t depth()
    {
        return count;
    }
    int32_t snapshot_into(T out[], int32_t maxLen)
    {
        int32_t = 0;
        Node* copy = top;
        while(copy && i < maxLen)
        {
            out[i]=copy->data;
            i++;
            copy=copy->next;
        }
        return i;
    }
};


// Timeline : doubly linked list of Snapshots
struct Snapshot; // fwd declaration;
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
struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};
struct TTDBHeader
{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};
void writeHeader(FILE *f, const TTDBHeader &h)
{
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);

    // placeholder for other two data members
}

// resolve.bin - bookkeeping
struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};



// PASS 0x0: READING source.bin + VALIDITY CHECK
bool readSourceLine(ifstream &in, string &out)
{
    string str;
    while(getline(in, s))
    {
        int32_t st =0, ed = s.size();
        while(st < ed && isspace((unsigned char) s[a]))
        {
            a++;
        }
        while(ed > a && isspace((unsigned char) s[b-1]))
        {
            b--;
        }
        if(st<ed)
        {
            out=s.substr(st,st-ed);
            return true;
        }
    }
    return false;
}
string firstWord(const string &line)
{
    int32_t i=0,n=line.size();
    while(i<n && line[i]== ' ')
    {
        i++;
    }
    int32_t k=i;
    while(k<n && line[k]!= ' ')
    {
        k++;
    }
    return line.substr(i,k-i);
}
string secondWord(const string &line)
{
    int32_t i=0,n=line.size();
    while(i<n && line[i]== ' ')
    {
        i++;
    }
    while(i<n && line[k]!= ' ')
    {
        i++;
    }
    while(i<n && line[i]== ' ')
    {
        i++;
    }
    int32_t j=i;
    while(j<n && line[j]!= ' ')
    {
        j++;
    }
    return line.substr(i,j-i);
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
    bool inside_function= false;
    int32_t ln = 0;
    while (readSourceLine(in, line))
    {
        ln++;
        string w = firstWord(line);
        if (w == "func")
        {
            if (inside_function)
            {
                cout << "error: nested func at line "<<ln<< endl;
                return false;
            }
            inside_function = true;
        }
        else if (w == "func_end")
        {
            if (!inside_function)
            {
                cout<< "error: func_end without func at line "<< ln<< endl;
                return false;
            }
            inside_function= false;
        }
    }
    if (inside_function)
    {
        cout<< "error: func without func_end"<< endl;
        return false;
    }
    return true;
}

// PASS 0x1: RESOLVE() -> resolve.bin
int64_t writeResolveRecord(FILE *f,int64_t offsetField,const string &text)
{
    int64_t pos=ftell(f);
    int32_t size=text.size();
    fwrite(&offsetField,8,1,f);
    fwrite(&size,4,1,f);
    fwrite(text.c_str(),1,size,f);
    return pos;
}

int64_t readResolveRecord(FILE *f,string &outText)
{
    int64_t sz;
    int32_t size;

    sz=ftell(f);

    if(fread(&size,4,1,f)!=1)
        return -1;

    if(size<0||size>100000)
        return -1;

    outText.resize(size);

    if(size>0&&fread(&outText[0],1,size,f)!=(size_t)size)
        return -1;

    return sz;
}

int64_t resolveProgram(const char *sourcePath,const char *resolveBinPath)
{
    FuncEntry funcArray[MAX_FUNCS];
    int32_t funcCount=0;

    PendingPatch patches[MAX_PATCHES];
    int32_t patchCount=0;

    ifstream in(sourcePath);

    if(!in)
    {
        cout<<"error: cannot open source file!"<<endl;
        return -1;
    }
    FILE *f=fopen(resolveBinPath,"wb+");
    if(!f)
    {
        cout<<"error: cannot create resolve.bin"<<endl;
        return -1;
    }
    string line;
    bool ok=true;

    while(ok&&readSourceLine(in,line))
    {
        string w=firstWord(line);

        int64_t field=ftell(f);

        if(w=="call")
            field=0;

        int64_t pos=writeResolveRecord(f,field,line);

        if(w=="func")
        {
            string functionName=secondWord(line);

            if(funcCount>=MAX_FUNCS)
            {
                cout<<"too many functions"<<endl;
                ok=false;
                break;
            }

            bool alreadyExists=false;

            for(int32_t i=0;i<funcCount;i++)
            {
                if(funcArray[i].funcName==functionName)
                {
                    alreadyExists=true;
                    break;
                }
            }

            if(alreadyExists)
            {
                cout<<"duplicate function "<<functionName<<endl;
                ok=false;
                break;
            }

            funcArray[funcCount].funcName=functionName;
            funcArray[funcCount].byteOffsetInResolveBin=pos;
            funcCount++;
        }
        else if(w=="call")
        {
            if(patchCount>=MAX_PATCHES)
            {
                cout<<"too many calls"<<endl;
                ok=false;
                break;
            }

            patches[patchCount].byteOffsetOfOffsetField=pos;
            patches[patchCount].targetFuncName=secondWord(line);
            patchCount++;
        }
    }
    in.close();
    int64_t mainOffset=-1;
    if(ok)
    {
        for(int32_t i=0;i<patchCount;i++)
        {
            string targetFunctionName=patches[i].targetFuncName;
            int64_t targetOffset=-1;

            for(int32_t j=0;j<funcCount;j++)
            {
                if(funcArray[j].funcName==targetFunctionName)
                {
                    targetOffset=funcArray[j].byteOffsetInResolveBin;
                    break;
                }
            }

            if(targetOffset==-1)
            {
                cout<<"call to undefined function "<<targetFunctionName<<endl;
                ok=false;
                break;
            }

            fseek(f,patches[i].byteOffsetOfOffsetField,SEEK_SET);
            fwrite(&targetOffset,8,1,f);
        }
    }
    if(ok)
    {
        for(int32_t i=0;i<funcCount;i++)
        {
            if(funcArray[i].funcName=="main")
            {
                mainOffset=funcArray[i].byteOffsetInResolveBin;
                break;
            }
        }

        if(mainOffset==-1)
            cout<<"main function not found"<<endl;
    }

    fclose(f);

    return mainOffset;
}

    // Every source line becomes one record holding the raw line, as-is.
    // resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
    // (remember its position) and CALL (remember which function it needs
    // and where its offset field sits).
    // Once the whole file is written, every CALL's offset field is patched
    // with its target's position. Patching happens after the full write
    // Returns the byte offset of main's FUNC header record.
    // if there is no main return the error 

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
}
void cpy(char *d, const string &s)
{
    strncpy(d, s.c_str(), 31);
    d[31] = 0;
}
Variable *findvar(Frame &fr, const string &name)
{
    for (int32_t i = 0; i < fr.argc; i++)
    {
        if (strcmp(fr.argv[i].name, name.c_str()) == 0)
            return &fr.argv[i];
    }
    for (int32_t i = 0; i < fr.localCount; i++)
    {
        if (strcmp(fr.locals[i].name, name.c_str()) == 0)
            return &fr.locals[i];
    }
    return nullptr;
}
bool getval(Frame &fr, const string &s, int32_t &v)
{
    if ((s[0] >= '0' && s[0] <= '9') || (s[0] == '-' && s.size() > 1))
    {
        v = atoi(s.c_str());
        return true;
    }
    Variable *p = findvar(fr, s);
    if (!p)
        return false;
    v = p->value;
    return true;
}
void executeProgram(const char *resolveBinPath, int64_t mainOffset, Timeline &timeline)
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
    cpy(mf.func_name, tk[1].text);
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
 
        if (k == "set")
        {
            int32_t v;
            if (n < 3 || !getval(cur, tk[2].text, v))
                er = "bad set";
            else
            {
                Variable *p = findvar(cur, tk[1].text);
                if (!p)
                {
                    if (cur.localCount >= MAX_VARS_PER_FRAME)
                        er = "too many variables";
                    else
                    {
                        p = &cur.locals[cur.localCount++];
                        cpy(p->name, tk[1].text);
                    }
                }
                if (p)
                    p->value = v;
            }
            pc = nx;
        }
        else if (k == "add" || k == "sub" || k == "mul" || k == "div")
        {
            int32_t a;
            Variable *p = n < 3 ? nullptr : findvar(cur, tk[1].text);
            if (!p || !getval(cur, tk[2].text, a))
                er = "bad arithmetic";
            else if (k == "div" && a == 0)
                er = "division by zero";
            else
            {
                switch (k[0])
                {
                case 'a':
                    p->value += a;
                    break;
                case 's':
                    p->value -= a;
                    break;
                case 'm':
                    p->value *= a;
                    break;
                case 'd':
                    p->value /= a;
                    break;
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
                cpy(nf.func_name, ht[1].text);
                nf.argc = ac;
                nf.returnLine = (int32_t)nx;
                for (int32_t i = 0; i < ac; i++)
                {
                    int32_t v;
                    if (!getval(cur, tk[2 + i].text, v))
                    {
                        er = "undefined argument";
                        break;
                    }
                    cpy(nf.argv[i].name, ht[2 + i].text);
                    nf.argv[i].value = v;
                    cpy(ca[d][i], tk[2 + i].text);
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
                    Variable *p = findvar(cl, ca[d][i]);
                    if (p)
                        p->value = done.argv[i].value;
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
    // placeholder for header
    // index array of the size of stepcount from the timeline
    // placing each snapshot in the file while maintaining the index(starting point of each nth snapshot)
    // after timeline add the index array i the file
    // update the header
}
// main section
int32_t main()
{

    if (!validateProgram("source.bin"))
    {
        // send an error response instead of a .tdbg file
        return 1;
    }

    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");

    Timeline timeline;
    executeProgram("resolve.bin", mainOffset, timeline);

    writeTdbg(timeline, "session.tdbg");

    return 0;
}