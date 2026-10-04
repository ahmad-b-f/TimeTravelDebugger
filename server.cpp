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
    // Implement these functions
    Timeline()
    {
    }
    void record(Snapshot *s)
    {
        // add record in the timeline
    }
    TimelineNode *begin()
    {
    }
    int32_t getStepCount()
    {
    }
};

// Core structs
struct Variable
{
    string name;
    int32_t value;
};
struct Frame
{
    string func_name;
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
    // first word is always a instruction keyword
    // instruction set = [func, func_end, call, set, add, sub, mul and div]
    // next word is identifier like name of a function, variable name
    // after identifier all are the params/arg, space separated
}
Snapshot *buildSnapshot(Stack<Frame> &callStack)
{
    // build the snapshot based on the callStack given
}
void executeProgram(const char *resolveBinPath, int64_t mainOffset, Timeline &timeline)
{
    // initialize the call stack
    // make the main frame
    // push main frame on the call stack

    // implementation:
    // execute line by line, and according to the keyword perform action
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