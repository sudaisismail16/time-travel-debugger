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
class Stack{
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
    { // initialize the stack
        top=nullptr;
        count=0;
    }
    void push(const T &val)
    {
        if(count>=MAX_STACK_DEPTH){
            return;
        }
        Node *temp=new Node{val,top};
        top=temp;
        count++;
        // pushes the value on the stack if max limit is not reached yet.
    }
    T pop()
    {
        if(top==nullptr){
            return T();
        }
        Node *temp =top;
        T val=temp->data;
        top=top->next;
        delete temp;
        count--;
        return val;
        // pop the top value on the stack
    }
    T &peek()
    {
        return top->data;
        // returns the top value on the stack
    }
    bool isEmpty()
    {
        return count==0;
    }
    int32_t depth()
    {
        return count;
    }
    int32_t snapshot_into(T out[], int32_t maxLen)
    {
        int32_t i=0;
        Node *curr=top;
        while(curr!=nullptr&&i<maxLen){
            out[i]=curr->data;
            curr=curr->next;
            i++;
        }
        return i;
        // copies every frame, top to bottom in the array given as a parameter
        // this is what buildSnapshot() call, returns count written
    }
};

// Timeline : doubly linked list of Snapshots
struct Snapshot; // fwd declaration;
struct TimelineNode{
    Snapshot *data;
    TimelineNode *next;
    TimelineNode *prev;
};
class Timeline{
    TimelineNode *head, *tail;
    int32_t stepCount;

public:
    // Implement these functions
    Timeline()
    {
        head=nullptr;
        tail=nullptr;
        stepCount=0;
    }
    void record(Snapshot *s)
    {
        TimelineNode *temp=new TimelineNode();
        temp->data=s;
        temp->next=nullptr;
        temp->prev=tail;
        if(tail==nullptr){
            head=temp;
        }
        else{
            tail->next=temp;
        }
        tail=temp;
        stepCount++;
        // add record in the timeline
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

// Core structs
struct Variable{
    string name;
    int32_t value;
};
struct Frame{
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};
struct Snapshot{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};
struct TTDBHeader{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};
void writeHeader(FILE *f, const TTDBHeader &h){
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);

    // placeholder for other two data members
}

// resolve.bin - bookkeeping
struct FuncEntry{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};

// PASS 0x0: READING source.bin + VALIDITY CHECK
bool readSourceLine(ifstream &in, string &out){
    // reads the next nonblank line
    string line;
    while (getline(in,line)){
        if(!line.empty()&&line.back()=='\r'){
            line.pop_back();
        }
        int32_t len=line.size();
        for(int32_t i=0;i<len;i++){
            if(line[i]!=' ' && line[i]!='\t'){
                out = line;
                return true;
            }
        }
    }
    return false;
}
string firstWord(const string &line){
    // returns first word from the input string
    int32_t len=line.size();
    int32_t i=0;
    while(i<len && (line[i] == ' ' || line[i]=='\t')){
        i++;
    }
    string word="";
    while(i<len && (line[i] != ' ' && line[i] != '\t')){
        word+=line[i];
        i++;
    }
    return word;
}
string secondWord(const string &line){
    // returns the second word
    string first=firstWord(line);
    if(first.empty()){
        return "";
    }
    int32_t pos=line.find(first);
    string rest=line.substr(pos+first.size());
    return firstWord(rest);
}
bool validateProgram(const char *sourcePath){
    // for each func defined there should be exactly one func_end and no nested funcs allowed - 
    ifstream in(sourcePath);
    if(!in.is_open()){
        cout<< "Error: cannot open "<<sourcePath<<endl;
        return false;
    }
    bool inside=false;
    int32_t lineNo=0;
    string line;
    while(readSourceLine(in,line)){
        lineNo++;
        string word=firstWord(line);
        if(word=="func"){
            if(inside){
                cout<<"Error: nested func at " << lineNo <<endl;
                return false;
            }
            inside=true;
        }
        else if(word=="func_end"){
            if(!inside){
                cout<<"Error: func_end without func at "<<lineNo << endl;
                return false;
            }
            inside=false;
        }
    }
    if(inside){
        cout<<"Error: func without func_end" << endl;
        return false;
    }
    return true;
}

// PASS 0x1: RESOLVE() -> resolve.bin
int64_t writeResolveRecord(FILE *f, int64_t offsetField, const string &text)
{
    // writes one [offset(8B)][size(4B)][string] record at the current file position
    // returns this record's own starting byte position
    int64_t start=ftell(f);
    int32_t size=text.size();
    fwrite(&offsetField,sizeof(int64_t),1,f);
    fwrite(&size,sizeof(int32_t),1,f);
    fwrite(text.c_str(),1,size,f);
    return start;
}
int64_t readResolveRecord(FILE *f, string &outText)
{
    // reads one record at the current position and advances past it, returns the offset field - the raw line text comes back untouched in outText.
    int64_t offsetField;
    int32_t size;
    if(fread(&offsetField,sizeof(int64_t),1,f)!=1){
        return -1;
    }
    if(fread(&size,sizeof(int32_t),1,f)!=1){
        return -1;
    }
    outText.resize(size);
    if(size>0 && (int32_t)fread(&outText[0],1,size,f)!=size){
        return -1;
    }
    return offsetField;
}
int64_t resolveProgram(const char *sourcePath, const char *resolveBinPath)
{
    FuncEntry funcArray[MAX_FUNCS];
    int32_t funcCount = 0;
    PendingPatch patches[MAX_PATCHES];
    int32_t patchCount = 0;
    // Every source line becomes one record holding the raw line, as-is.
    // resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
    // (remember its position) and CALL (remember which function it needs
    // and where its offset field sits).
    // Once the whole file is written, every CALL's offset field is patched
    // with its target's position. Patching happens after the full write
    // Returns the byte offset of main's FUNC header record.
    // if there is no main return the error 
    ifstream in(sourcePath);
    if(!in.is_open()){
        cout<<"Error: cant open "<<sourcePath<<endl;
        return -1;
    }
    FILE *out=fopen(resolveBinPath,"w+b");
    if(out==nullptr){
        cout<<"Error: cant create "<<resolveBinPath<<endl;
        return -1;
    }


    string line;
    while(readSourceLine(in,line)){
        string word=firstWord(line);
        int64_t start=ftell(out);
        if(word=="func" && funcCount<MAX_FUNCS){
            funcArray[funcCount].funcName =secondWord(line);
            funcArray[funcCount].byteOffsetInResolveBin = start;
            funcCount++;
        }
        else if(word == "call" && patchCount<MAX_PATCHES){
            patches[patchCount].byteOffsetOfOffsetField=start;
            patches[patchCount].targetFuncName=secondWord(line);
            patchCount++;
        }
        writeResolveRecord(out,start,line);
    }
    in.close();

    for(int32_t i=0;i<patchCount;i++){
        int32_t found=-1;
        for(int32_t j = 0;j<funcCount;j++){
            if(funcArray[j].funcName==patches[i].targetFuncName){
                found = j;
                break;
            }
        }
        if(found==-1){
            cout<<"Error: called undefined func "<<patches[i].targetFuncName<<endl;
            fclose(out);
            return -1;
        }
        fseek(out,patches[i].byteOffsetOfOffsetField,SEEK_SET);
        fwrite(&funcArray[found].byteOffsetInResolveBin,sizeof(int64_t),1,out);
    }
    int64_t mainOffset = -1;
    for (int32_t j = 0; j < funcCount; j++)
    {
        if (funcArray[j].funcName == "main")
            mainOffset = funcArray[j].byteOffsetInResolveBin;
    }
    fclose(out);

    if (mainOffset == -1)
    {
        cout << "Error: no main function" << endl;
        return -1;
    }
    return mainOffset;
}
//helper function
Variable *findVar(Frame &fr, const string &name){
    for (int32_t i = 0; i < fr.argc; i++){
        if (fr.argv[i].name == name){
            return &fr.argv[i];
        }
    }
    for (int32_t i = 0; i < fr.localCount; i++){
        if (fr.locals[i].name == name){
            return &fr.locals[i];
        }
    }
    return nullptr;
}

bool isNumber(const string &s){
    if (s.empty()){
        return false;
    }
    int32_t i = 0;
    if (s[0] == '-' && s.size() > 1){
        i = 1;
    }
    for (; i < (int32_t)s.size(); i++){
        if (s[i] < '0' || s[i] > '9'){
            return false;
        }
    }
    return true;
}

int32_t getValue(Frame &fr, const string &text, bool &ok){
    if (isNumber(text)){
        ok = true;
        return stoi(text);
    }
    Variable *v = findVar(fr, text);
    if (v == nullptr){
        ok = false;
        return 0;
    }
    ok = true;
    return v->value;
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
int32_t tokenizeLine(const string &line, Token tokens[], int32_t maxTokens){
    // first word is always a instruction keyword
    // instruction set = [func, func_end, call, set, add, sub, mul and div]
    // next word is identifier like name of a function, variable name
    // after identifier all are the params/arg, space separated
    int32_t len = line.size();
    int32_t i = 0;
    int32_t count = 0;
    while (i < len && count < maxTokens){
        while (i < len && (line[i] == ' ' || line[i] == '\t')){
            i++;
        }
        if (i >= len){
            break;
        }
        string word = "";
        while (i < len && line[i] != ' ' && line[i] != '\t'){
            word += line[i];
            i++;
        }
        if (count == 0){
            tokens[count].type = KEYWORD;
        }
        else if (count == 1){
            tokens[count].type = IDENTIFIER;
        }
        else{
            tokens[count].type = PARAM;
        }
        tokens[count].text = word;
        count++;
    }
    return count;
}
Snapshot *buildSnapshot(Stack<Frame> &callStack){
    // build the snapshot based on the callStack given
    Snapshot *s = new Snapshot();
    s->stackDepth = callStack.snapshot_into(s->callStack, MAX_STACK_DEPTH);
    return s;
}
void executeProgram(const char *resolveBinPath, int64_t mainOffset, Timeline &timeline){
    // initialize the call stack
    // make the main frame
    // push main frame on the call stack

    // implementation:
    // execute line by line, and according to the keyword perform action
    FILE *f = fopen(resolveBinPath, "rb");
    if (f == nullptr){
        cout << "Error: cannot open " << resolveBinPath << endl;
        return;
    }
    Stack<Frame> callStack;
    string argNames[MAX_STACK_DEPTH][MAX_VARS_PER_FRAME];
    Frame mainFrame = Frame();
    mainFrame.func_name = "main";
    mainFrame.argc = 0;
    mainFrame.localCount = 0;
    mainFrame.returnLine = -1;
    callStack.push(mainFrame);
    string line;
    fseek(f, mainOffset, SEEK_SET);
    readResolveRecord(f, line);
    while (!callStack.isEmpty()){
        int64_t offsetField = readResolveRecord(f, line);
        if (offsetField == -1){
            cout << "Error: file ended before main finished" << endl;
            break;
        }
        Token tokens[MAX_TOKENS];
        int32_t n = tokenizeLine(line, tokens, MAX_TOKENS);
        if (n == 0){
            continue;
        }
        string kw = tokens[0].text;
        if (kw == "set"){
            if (n < 3){
                cout << "Error: set needs a name and a value" << endl;
                fclose(f);
                return;
            }
            Frame &cur = callStack.peek();
            bool ok;
            int32_t value = getValue(cur, tokens[2].text, ok);
            if (!ok){
                cout << "Error: unknown variable " << tokens[2].text << endl;
                fclose(f);
                return;
            }
            Variable *v = findVar(cur, tokens[1].text);
            if (v == nullptr){
                if (cur.localCount >= MAX_VARS_PER_FRAME){
                    cout << "Error: too many variables in " << cur.func_name << endl;
                    fclose(f);
                    return;
                }
                v = &cur.locals[cur.localCount];
                v->name = tokens[1].text;
                cur.localCount++;
            }
            v->value = value;
        }
        else if (kw == "add" || kw == "sub" || kw == "mul" || kw == "div"){
            if (n < 3){
                cout << "Error: " << kw << " needs two operands" << endl;
                fclose(f);
                return;
            }
            Frame &cur = callStack.peek();
            Variable *target = findVar(cur, tokens[1].text);
            if (target == nullptr){
                cout << "Error: unknown variable " << tokens[1].text << endl;
                fclose(f);
                return;
            }
            bool ok;
            int32_t value = getValue(cur, tokens[2].text, ok);
            if (!ok){
                cout << "Error: unknown variable " << tokens[2].text << endl;
                fclose(f);
                return;
            }
            if (kw == "add"){
                target->value += value;
            }
            else if (kw == "sub"){
                target->value -= value;
            }
            else if (kw == "mul"){
                target->value *= value;
            }
            else{
                if (value == 0){
                    cout << "Error: division by zero" << endl;
                    fclose(f);
                    return;
                }
                target->value /= value;
            }
        }

        else if (kw == "call"){
            if (n < 2){
                cout << "Error: call needs a function name" << endl;
                fclose(f);
                return;
            }
            Frame &caller = callStack.peek();
            int32_t argCount = n - 2;
            int32_t d = callStack.depth();
            if (d >= MAX_STACK_DEPTH){
                cout << "Error: stack overflow (too many nested calls)" << endl;
                fclose(f);
                return;
            }
            long returnPos = ftell(f);
            fseek(f, offsetField, SEEK_SET);
            string header;
            readResolveRecord(f, header);
            Token ptoks[MAX_TOKENS];
            int32_t pn = tokenizeLine(header, ptoks, MAX_TOKENS);
            if (pn - 2 != argCount){
                cout << "Error: wrong number of arguments for " << tokens[1].text << endl;
                fclose(f);
                return;
            }
            Frame nf = Frame();
            nf.func_name = tokens[1].text;
            nf.argc = argCount;
            nf.localCount = 0;
            nf.returnLine = (int32_t)returnPos;
            for (int32_t i = 0; i < argCount; i++){
                bool ok;
                nf.argv[i].name = ptoks[2 + i].text;
                nf.argv[i].value = getValue(caller, tokens[2 + i].text, ok);
                if (!ok){
                    cout << "Error: unknown variable " << tokens[2 + i].text << endl;
                    fclose(f);
                    return;
                }
                argNames[d][i] = tokens[2 + i].text;
            }
            callStack.push(nf);
        }
        else if (kw == "func_end"){
            int32_t depthNow = callStack.depth();
            Frame done = callStack.pop();
            if (depthNow > 1){
                Frame &caller = callStack.peek();
                for (int32_t i = 0; i < done.argc; i++){
                    Variable *v = findVar(caller, argNames[depthNow - 1][i]);
                    if (v != nullptr){
                        v->value = done.argv[i].value;
                    }
                }
                fseek(f, done.returnLine, SEEK_SET);
            }
        }
        else{
            cout << "Error: unknown instruction " << kw << endl;
            fclose(f);
            return;
        }
        timeline.record(buildSnapshot(callStack));
    }
    fclose(f);
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
    if (mainOffset == -1){
        return 1;
    }
    Timeline timeline;
    executeProgram("resolve.bin", mainOffset, timeline);

    writeTdbg(timeline, "session.tdbg");

    return 0;
}