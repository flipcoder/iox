#include <iostream>
#include <stack>
#include <vector>
#include <fstream>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <thread>
#include <boost/scope_exit.hpp>
#include <readline/readline.h>
#include <readline/history.h>
using namespace std;

#include <boost/any.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>
#include "kit/args/args.h"
#include "kit/async/async.h"
#include "kit/kit.h"

#include "info.h"

using boost::lexical_cast;

static const char USAGE[] =
R"(iox

    iox langauge interpreter

    Usage:
      iox <script>...

    Options:
      -h --help     Show this screen.
      --version     Show version.
)";

struct Variable
{
    enum ID {
        Int = 0,
        String,
        Real,
        Bool,
        List,
        IO
    };
    enum Wrapper {
        PRIMITIVE = 0,
        ADDRESS = kit::bit(0),
        REACTIVE = kit::bit(1)
    };
    
    //Variable(){
    //}
    Variable(boost::any v, ID t):
        val(v),
        type(t),
        wrapper(0)
    {}

    Variable(Variable&&) = default;
    Variable& operator=(Variable&&) = default;
    Variable(const Variable&) = default;
    Variable& operator=(const Variable&) = default;

    ID type;
    unsigned wrapper;
    
    std::string name; // reflection!
    boost::any val;
};

vector<string> m_TypeNames {
    "int",
    "str",
    "real",
    "bool",
    "list",
    "io"
};

struct Mark
{
    //std::string name;
    //std::string fn;
    //unsigned ln = 0;
    //unsigned tok = 0;
    fstream::pos_type seekpos;
};

struct Context
{
    bool inter = false;
    unsigned ln = 0;
    unsigned tok = 0;
    
    bool can_jump = false;
    function<void(fstream::pos_type)> jump_func;
    
    fstream::pos_type seekpos;
    
    vector<Variable> m_Cycled;
    stack<vector<Variable>> m_Stream;
    unordered_map<string, vector<Variable>> m_Stack;
    std::unordered_map<std::string, Mark> m_Marks;
    unordered_map<string, function<void()>> m_Funcs;

    Context(const Context&) = default;
    Context& operator=(const Context&) = default;
    Context(Context&&) = default;
    Context& operator=(Context&&) = default;

    std::vector<char*> rl_history;

    void recycle()
    {
        flush();
        m_Stream.push(move(m_Cycled));
        m_Cycled.clear();
    }
    
    void cycle()
    {
        if(m_Stream.empty())
        {
            m_Stream.push(vector<Variable>());
        }
        else
        {
            m_Cycled = move(m_Stream.top());
            m_Stream.top().clear();
        }
    }
    
    void flush()
    {
        if(m_Stream.empty())
            m_Stream.push(vector<Variable>());
        else
            m_Stream.top().clear();
    }
    
    bool append=false;
    
    void push_stream()
    {
        m_Stream.push(vector<Variable>());
    }
    void pop_stream()
    {
        m_Stream.pop();
    }
    
    void clear()
    {
        kit::clear(m_Stream);
        kit::clear(m_Stack);
        flush();
    }

    void choice()
    {
        int cid = std::rand() % m_Stream.top().size();
        m_Stream.top() = vector<Variable>(
            m_Stream.top().begin() + cid, m_Stream.top().begin() + cid + 1
        );
    }
    
    void randint()
    {
        int s = boost::any_cast<int>(m_Stream.top().at(0).val);
        int e = boost::any_cast<int>(m_Stream.top().at(1).val);
        int a = (std::rand() % (e+1-s)) + s;
        flush();
        push<int>(a, Variable::Int);
    }
        
    void sleep()
    {
        int sec = boost::any_cast<int>(m_Stream.top().at(0).val);
        flush();
        std::this_thread::sleep_for(std::chrono::seconds(sec));
    }
    
    void in()
    {
        if(not m_Stream.top().empty())
            out("", false);
        string line;
        std::getline(cin, line);
        //char* rl = readline("");
        //BOOST_SCOPE_EXIT_ALL() {
        //    free(rl);
        //};
        flush();
        m_Stream.top().push_back(Variable(
            boost::any(line),
            Variable::String
        ));
    }
    
    void out(
        std::string sep = "",
        bool newline = true,
        bool quotestrings = false
    ){
        try{
            auto& s = m_Stream.top();
            size_t sz = s.size();
            for(size_t i=0; i < sz; ++i)
            {
                if(i) cout << sep;
                auto& d = s[i].val;
                
                switch(s[i].type)
                {
                    case Variable::String:
                        // encode escaped strings
                        if(quotestrings)
                            cout << "\'";
                        cout << boost::any_cast<string>(d);
                        if(quotestrings)
                            cout << "\'";
                        break;
                    case Variable::Int:
                        cout << boost::any_cast<int>(d);
                        break;
                    case Variable::Real:
                        cout << std::showpoint << boost::any_cast<float>(d);
                        break;
                    case Variable::Bool:
                    {
                        bool b = boost::any_cast<bool>(d);
                        cout << (b?"true":"false");
                        break;
                    }
                    default:
                        assert(false);
                        break;
                };
            }
            if(newline)
                cout << endl;
        }catch(const std::out_of_range&){
            if(newline)
                cout << endl;
        }catch(const boost::bad_any_cast&){
            assert(false);
        }
    }
    
    void seq()
    {
        int st = boost::any_cast<int>(m_Stream.top().at(0).val);
        int en;
        try{
            en = boost::any_cast<int>(m_Stream.top().at(1).val);
        }catch(const std::out_of_range&){
            // if only 1 arg, seq 5 is range [0,5)
            en = st;
            st = 1;
        }
        int inc = st <= en ? 1 : -1;
        TRY(inc = boost::any_cast<int>(m_Stream.top().at(2).val));
        // adjust end point
        flush();
        for(int i=st; (en > st) ? i <= en : i >= en; i += inc)
            m_Stream.top().push_back(Variable(boost::any(i), Variable::Int));
    }
    
    void length()
    {
        int len = (int)m_Stream.top().size();
        flush();
        m_Stream.top().push_back(Variable(boost::any(
            len
        ), Variable::Int));
    }
    
    void flip()
    {
        std::reverse(ENTIRE(m_Stream.top()));
    }
    
    void rev()
    {
        auto st = move(m_Stream.top());
        size_t sz = st.size();
        for(size_t i=0; i < sz; ++i)
        {
            auto& d = st[i].val;
            string s = boost::any_cast<string>(d);
            std::reverse(ENTIRE(s));
            m_Stream.top().push_back(Variable(boost::any(s), Variable::String));
        }
    }

    void abs()
    {
        auto& s = m_Stream.top();
        size_t sz = s.size();
        for(size_t i=0; i < sz; ++i)
        {
            auto& d = s[i].val;
            switch(s[i].type)
            {
                case Variable::Int:
                {
                    d = boost::any(std::abs(boost::any_cast<int>(d)));
                    break;
                }
                case Variable::Real:
                {
                    d = boost::any(std::abs(boost::any_cast<float>(d)));
                    break;
                }
                default:
                    assert(false);
                    break;
            };
        }
    }
    
    //void each(const std::function<unsigned, boost::any>& cb)
    //{
    //    auto& s = m_Stream.top();
    //    size_t sz = s.size();
    //    for(unsigned i=0; i < sz; ++i)
    //        cb(i, s[i]);
    //}
    
    void cast_int()
    {
        auto& s = m_Stream.top();
        size_t sz = s.size();
        for(size_t i=0; i < sz; ++i)
        {
            auto& d = s[i].val;
            
            switch(s[i].type)
            {
                case Variable::String:
                    d = boost::any(boost::lexical_cast<int>(boost::any_cast<string>(d)));
                    break;
                case Variable::Int:
                    // already int
                    break;
                case Variable::Real:
                {
                    float f = boost::any_cast<float>(d);
                    f = (f > 0.0) ? (f + 0.5) : (f - 0.5);
                    d = boost::any((int)f);
                    break;
                }
                case Variable::Bool:
                {
                    bool b = boost::any_cast<bool>(d);
                    d = boost::any(b?1:0);
                    break;
                }
                default:
                    assert(false);
                    break;
            };
            s[i].type = Variable::Int;
        }
    }
    
    void cast_real(){}
    void cast_str()
    {
    }
    void cast_bool(){
        auto& s = m_Stream.top();
        size_t sz = s.size();
        for(size_t i=0; i < sz; ++i)
        {
            auto& d = s[i].val;
            
            switch(s[i].type)
            {
                case Variable::String:
                {
                    string s = boost::any_cast<string>(d);
                    d = boost::any(not s.empty());
                    break;
                }
                case Variable::Int:
                {
                    d = boost::any(!! boost::any_cast<int>(d));
                    break;
                }
                default:
                    assert(false);
                    break;
            };
            s[i].type = Variable::Bool;
        }

    }
    
    bool q()
    {
        cast_bool();
        bool b = boost::any_cast<bool>(m_Stream.top().at(0).val);
        if(b)
            return true;
        return false;
    }
    
    template<class T>
    bool cmpt(std::vector<Variable>& st)
    {
        size_t sz = st.size();
        bool wrong_type = false;
        bool good = true;
        try{
            T last = T();
            for(size_t i=0; i < sz; ++i)
            {
                T b = boost::any_cast<T>(st[i].val);
                if(i)
                {
                    if(b != last)
                    {
                        good = false;
                        break;
                    }
                }
                last = b;
            }
        }catch(const std::out_of_range&){
        }catch(const boost::bad_any_cast&){
            wrong_type = true;
        }
        if(not wrong_type)
        {
            flush();
            push<bool>(good, Variable::Bool);
            return true;
        }
        return false;
    }
        
    void gt() {}
    void lt() {}
    void gte() {}
    void lte() {}
    
    void cmp()
    {
        auto st = move(m_Stream.top());
        if(cmpt<bool>(st))
            return;
        if(cmpt<int>(st))
            return;
        if(cmpt<string>(st))
            return;
        assert(false);
    }
    
    void notop()
    {
        cast_bool(); // ensure all stream elements are actually of type bool
        
        auto& st = m_Stream.top();
        try{
            size_t sz = st.size();
            for(size_t i=0; i < sz; ++i)
            {
                st[i].val = boost::any_cast<bool>(not 
                    boost::any_cast<bool>(st[i].val)
                );
            }
        }catch(const std::out_of_range&){
        }catch(const boost::bad_any_cast&){
            assert(false);
        }
    }
    
    void assert_this()
    {
        auto last_st = m_Stream.top();
        cast_bool();
        auto st = m_Stream.top();
        
        try{
            size_t sz = st.size();
            for(size_t i=0; i < sz; ++i)
            {
                if(not boost::any_cast<bool>(st[i].val))
                    throw runtime_error((boost::format(
                        "assertion failed @ ln %s"
                    ) % ln).str());
            }
        }catch(const std::out_of_range&){
        }catch(const boost::bad_any_cast&){
            assert(false);
        }
        m_Stream.top() = last_st;
    }
    
    void sum()
    {
        auto st = move(m_Stream.top());
        flush();
        int tot = 0;
        //Variable::Type t = Variable::Int;
        try{
            size_t sz = st.size();
            for(size_t i=0; i < sz; ++i)
            {
                //tot += boost::any_cast<int>(st[i].val);
                //switch(st[i].type)
                //{
                    //case Variable::Int:
                        tot += boost::any_cast<int>(st[i].val);
                        //break;
                    //case Variable::Real:
                    //    tot += boost::any_cast<float>(st[i].val);
                    //    m_Stream.top().push_back(Variable(boost::any(tot), Variable::Real));
                    //    break;
                    //default:
                    //    assert(false);
                    //    break;
                //}
            }
        }catch(const std::out_of_range&){
        }catch(const boost::bad_any_cast&){
            assert(false);
        }
        m_Stream.top().push_back(Variable(boost::any(tot), Variable::Int));
    }
    
    void diff()
    {
        auto st = move(m_Stream.top());
        flush();
        int tot = 0;
        try{
            size_t sz = st.size();
            for(size_t i=0; i < sz; ++i)
            {
                //switch(st[i].type)
                //{
                //    case Variable::Int:
                if(i)
                    tot -= boost::any_cast<int>(st[i].val);
                else
                    tot += boost::any_cast<int>(st[i].val);
                
                        //break;
                    //case Variable::Real:
                    //    tot -= boost::any_cast<float>(st[i].val);
                    //    break;
                    //default:
                    //    assert(false);
                    //    break;
                //}
            }
        }catch(const std::out_of_range&){
        }catch(const boost::bad_any_cast&){
            assert(false);
        }
        m_Stream.top().push_back(Variable(boost::any(tot), Variable::Int));
    }
    void mult()
    {
        auto st = move(m_Stream.top());
        flush();
        int tot = 1;
        //Variable::Type t = Variable::Int;
        
        try{
            size_t sz = st.size();
            for(size_t i=0; i < sz; ++i)
            {
            //    switch(st[i].type)
            //    {
            //        case Variable::Int:
                          tot *= boost::any_cast<int>(st[i].val);
                //        break;
                //    case Variable::Real:
                //        tot *= boost::any_cast<float>(st[i].val);
                //        t = Variable::Float;
                //        break;
                //    default:
                //        assert(false);
                //        break;
                //}
            }
        }catch(const std::out_of_range&){
        }catch(const boost::bad_any_cast&){
            assert(false);
        }
        m_Stream.top().push_back(Variable(boost::any(tot), Variable::Int));
    }
    
    void div()
    {
        auto st = move(m_Stream.top());
        flush();
        int tot = 1;
        try{
            size_t sz = st.size();
            for(size_t i=0; i < sz; ++i)
            {
                //switch(st[i].type)
                //{
                    //case Variable::Int:
                    //{
                        int a = boost::any_cast<int>(st[i].val);
                        if(a == 0)
                            throw std::runtime_error("divide by zero");
                        tot /= a;
                    //    break;
                    //}
                    //case Variable::Real:
                    //    tot /= boost::any_cast<float>(st[i].val);
                    //    m_Stream.top().push_back(Variable(boost::any(tot), Variable::Real));
                    //    break;
                    //default:
                    //    assert(false);
                    //    break;
                //}
            }
        }catch(const std::out_of_range&){
        }catch(const boost::bad_any_cast&){
            assert(false);
        }
        m_Stream.top().push_back(Variable(boost::any(tot), Variable::Int));
    }

    //std::string ret()
    //{
        
    //}
    
    template<class T>
    void push(T s, Variable::ID tid = Variable::String)
    {
        m_Stream.top().push_back(Variable(s, tid));
    }
    
    template<class T>
    bool try_push(string s, Variable::ID tid, bool append_this = false)
    {
        T cast;
        try{
            cast  = lexical_cast<T>(s);
        }catch(...){
            return false;
        }

        if(not append_this)
        {
            //if(not m_Stream.top().empty())
            //{
            //    throw std::runtime_error((boost::format(
            //        "literal \'%s\' is not assignable"
            //    ) % s).str());
            //}

            cycle();//flush();
        }
        
        push<T>(cast, tid);
        return true;
    }
    
    void reset()
    {
        append = false;
        clear();
    }
    
    void noop(){}
    void ncmp(){cmp(); notop();}
    void out_np(){out();}
    void dbg(){out(", ", true, true);}
    void type(){
        auto st = move(m_Stream.top());
        for(auto&& t: st)
            push<string>(m_TypeNames[t.type]);
    }
    
    void front(){
        auto e = m_Stream.top().front();
        flush();
        m_Stream.top().push_back(e);
    }
    void back(){
        auto e = m_Stream.top().back();
        flush();
        m_Stream.top().push_back(e);
    }
    
    void join(){
        auto st = m_Stream.top();
        flush();
        auto b = boost::any_cast<string>(st.back().val);
        st.pop_back();
        vector<string> tokens;
        transform(ENTIRE(st), tokens.begin(), [](Variable& v){
            return boost::any_cast<string>(v.val);
        });
        push<string>(boost::join(tokens, b), Variable::String);
    }
    
    void take(){
        auto st = move(m_Stream.top());
        auto sz = st.size() - 1; // cut off count
        flush();
        // get count
        int b = boost::lexical_cast<int>(
            boost::any_cast<int>(st.back().val)
        );
        if(b<1)
            throw std::out_of_range("slice length out of range");

        // slice
        m_Stream.top() = std::vector<Variable>(
            st.begin(), st.begin() + std::min<int>(b,sz)
        );
    }
    
    void mark(){
        m_Marks[
            boost::any_cast<string>(m_Stream.top().at(0).val)
        ] = { seekpos };
    }
    void goto_mark()
    {
        if(can_jump){
            string n = boost::any_cast<string>(m_Stream.top().at(0).val);
            auto m = m_Marks.find(n);
            if(m != m_Marks.end()){
                jump_func(m->second.seekpos);
            }else{
                throw std::runtime_error((boost::format(
                    "no such mark \'%s\'"
                    ) % n
                ).str());
            }
        }else{
            throw std::runtime_error("marks feature unavailable");
        }
    }
    
    Context() {
        m_Funcs = {
            {"out", std::bind(&Context::out_np,this)},
            {"in", std::bind(&Context::in,this)},
            {"dbg", std::bind(&Context::dbg,this)},
            {"?", std::bind(&Context::q,this)},
            {"not", std::bind(&Context::notop,this)},
            {"assert", std::bind(&Context::assert_this,this)},
            {"!", std::bind(&Context::notop,this)},
            {"else", std::bind(&Context::noop,this)},
            {"sleep", std::bind(&Context::sleep,this)},
            {"len", std::bind(&Context::length,this)},
            {"int", std::bind(&Context::cast_int,this)},
            {"real", std::bind(&Context::cast_real,this)},
            {"str", std::bind(&Context::cast_str,this)},
            {"bool", std::bind(&Context::cast_bool,this)},
            {"!!", std::bind(&Context::cast_bool,this)},
            {"+", std::bind(&Context::sum,this)},
            {"-", std::bind(&Context::diff,this)},
            {"*", std::bind(&Context::mult,this)},
            {"/", std::bind(&Context::div,this)},
            {"_", std::bind(&Context::noop,this)},
            {"flip", std::bind(&Context::flip,this)},
            {"rev", std::bind(&Context::rev,this)},
            {"seq", std::bind(&Context::seq,this)},
            {"<=", std::bind(&Context::lte,this)},
            {">=", std::bind(&Context::gte,this)},
            {"<", std::bind(&Context::lt,this)},
            {">", std::bind(&Context::gt,this)},
            {"==", std::bind(&Context::cmp,this)},
            {"!=", std::bind(&Context::ncmp,this)},
            {"rand", std::bind(&Context::randint,this)},
            {"choice", std::bind(&Context::choice,this)},
            {"type", std::bind(&Context::type,this)},
            {"mark", std::bind(&Context::mark,this)},
            {"jmp", std::bind(&Context::goto_mark,this)},
            {"join", std::bind(&Context::join,this)},
            {"take", std::bind(&Context::take,this)}
        };
    }

    ~Context() {
        for(char* c: rl_history)
            free(c);
        rl_history.clear();
    }
    
    bool token(std::string s)
    {
        if(s.empty())
            return true;
        auto len = s.length();
        
        bool append_this = append;
        
        if(s[len-1]==',')
        {
            append = true;
            s = s.substr(0, --len); // cut comma
        }
        else
        {
            append = false;
        }
        
        // string
        if(s[0] == '\"' || s[0] == '\'')
        {
            s = s.substr(1, len -= 2); // cut quotes
            if(not append_this)
                cycle();
            push<string>(s);
            return true;
        }

        if(s=="_")
        {
            if(not append_this)
                cycle();
            copy(ENTIRE(m_Cycled), back_inserter(m_Stream.top()));
        }
        
        if(s=="false")
        {
            if(not append_this)
                cycle();
            push<bool>(false, Variable::Bool);
            return true;
        }
        else if(s=="true")
        {
            if(not append_this)
                cycle();
            push<bool>(true, Variable::Bool);
            return true;
        }

        // var
        if(s[0]=='$')
        {
            // put var in stream
            s = s.substr(1);

            // set
            if(not m_Stream.top().empty())
            {
                if(not append_this)
                {
                    m_Stack[s] = m_Stream.top();
                }
                else
                {
                    copy(ENTIRE(m_Stack[s]), back_inserter(m_Stream.top()));
                }
            }
            else // stream empty?
            {
                // get
                try{
                    flush();
                    copy(
                        ENTIRE(m_Stack.at(s)),
                        back_inserter(m_Stream.top())
                    );
                }catch(const std::exception& e){
                    throw std::runtime_error((boost::format(
                        "no such variable \'%s\'"
                        ) % s
                    ).str());
                }
            }
            return true;
        }
        
        if(try_push<int>(s, Variable::Int, append_this))
            return true;

        if(try_push<float>(s, Variable::Real, append_this))
            return true;
        
        // function (TEMP, we'll make a umap lookup soon)
        //if(s=="out") out();
        //else if(s=="dbg") out(", ", true, true);
        //else if(s=="?") {
        //    if(!q())
        //        return false;
        //} else if(s=="not") notop();
        //else if(s=="else") {}
        //else if(s=="in") in();
        //else if(s=="sleep") sleep();
        //else if(s=="len") length();
        //else if(s=="int") cast_int();
        //else if(s=="real") cast_real();
        //else if(s=="str") cast_str();
        //else if(s=="bool") cast_bool();
        //else if(s=="+") sum();
        //else if(s=="-") diff();
        //else if(s=="*") mult();
        //else if(s=="/") div();
        //else if(s=="_") {}
        //else if(s=="flip") flip();
        //else if(s=="rev") rev();
        //else if(s=="seq") seq();
        //else if(s=="<=") lte();
        //else if(s==">=") gte();
        //else if(s==">") gt();
        //else if(s=="<") lt();
        //else if(s=="==") cmp();
        //else if(s=="!=") { cmp(); notop(); }
        //else if(s=="rand") randint();
        ////else if(s=="dist") dist();
        //else if(s=="type") {
        //    auto st = move(m_Stream.top());
        //    for(auto&& t: st)
        //        push<string>(m_TypeNames[t.type]);
        //} else if(s==";") {
        //    flush();
        
        auto func = m_Funcs.find(s);
        if(func != m_Funcs.end())
            func->second();
        else
        {
            throw std::runtime_error((boost::format(
                "no such function \'%s\'"
                ) % s
            ).str());
        }
        
        return true;
    }
};

int main(int argc, const char *argv[])
{
    std::srand(std::time(0));
    
    Args args(argc,argv,USAGE);

    if(args.has('v', "version"))
    {
        cout << Info::Program << " version " << Info::Version << endl;
        return 0;
    }
    if(args.has('h',"help"))
    {
        cout << USAGE << endl;
        return 0;
    }
    
    auto len = args.size();
    bool inter = (len==0); // interactive mode
    for(int i=0;;++i)
    {
        if(not inter && i >= args.size())
            break;
        
        Context ctx;
        ctx.inter = inter;
        ctx.can_jump = not inter;
        
        ifstream file;
        ctx.jump_func = [&file](fstream::pos_type pos){
            file.seekg(pos);
        };
        
        if(not inter)
        {
            file.open(args.at(i));
            if(not file.is_open())
                return 1;
        }
        
        string line;
        string last_line;
        string token;
        int indent = 0;
        int skip_until_indent = -1;
        int indent_rel = 0;
        
        ctx.clear();
        
        for(int ln=0;;++ln)
        {
            ctx.ln = ln;
            
            if(inter)
            {
                char* rl = readline("iox> ");
                line = string(rl);
                add_history(rl);
            }else{
                if(!std::getline(file, line))
                    return 0;
                ctx.seekpos = file.tellg();
            }
            
            if(inter)
            {
                if(line.empty())
                    line = last_line;
                else
                    last_line = line;
            }

            auto ind = line.find_first_not_of(" \t");
            if(ind == std::string::npos)
                continue;
            
            line = line.substr(line.find_first_not_of(" \t"));
            
            if(line[0] == '#')
                continue;
            
            if(skip_until_indent >= 0)
            {
                if(ind <= skip_until_indent)
                {
                    skip_until_indent = -1;
                    //cout << "resuming" << endl;
                }
                else
                    continue;
            }
            
            int last_indent = indent;
            indent = ind;
            
            // store rel indent from last executed line
            // this does not include skipped lines
            indent_rel = indent - last_indent;

            if(inter)
                if(not boost::ends_with(line, " out"))
                    line += " dbg";

            unsigned s;
            unsigned e = 0;
            
            bool eol = false;
            
            for(int tok_n=0;;++tok_n)
            {
                ctx.tok = tok_n;
                
                s = e;
                
                // find [s,e) of token
                bool in_quote = false;
                while(true){
                    char c;
                    try{
                        c = line.at(e);
                    }catch(const std::out_of_range&){
                        if(in_quote){
                            cerr << "quote parse issue @ " << ln << endl;
                            cerr << "    " << line << endl;
                            return 1;
                        }
                        eol = true;
                        break;
                    }
                    ++e;
                    if(c=='\"' || c=='\'')
                        in_quote = !in_quote;
                    else if(!in_quote)
                        if(c==' ' || c==',')
                            break;
                }
                
                string token = line.substr(s,e-s);
                
                boost::trim(token);
                
                if(not token.empty())
                {
                    if(tok_n == 0) // first token on line
                    {
                        if(token=="_")
                        {
                            ctx.recycle();
                        }
                        else
                        {
                            ctx.cycle();
                        }
                        
                        if(token=="else")
                        {
                            //cout << "else" << endl;
                            //cout << "indent=" << indent << endl;
                            //cout << "id=" << indent_rel << endl;
                            if(indent_rel == 0)
                            {
                                skip_until_indent = indent;
                                eol = true;
                                //cout << "skipping else" << endl; break;
                            }
                        }

                    }
                    
                    //cout << "[" << s << "," << e << ") t: " << token << endl;
                    try {
                        if(not ctx.token(token))
                        {
                            skip_until_indent = indent;
                            eol = true; // short circuit
                        }
                    } catch(const exception& e) {
                        if(inter)
                        {
                            cerr << e.what() << endl;
                            break;
                        }
                        else
                            throw;
                    }
                    ++tok_n;
                }
                
                if(eol)
                    break;
            }

            if(inter)
            {
                // output all stream line data
                //ctx.out()
            }

            //boost::tokenizer<boost::escaped_list_separator<char>> tokens(line,els);
            
            //for(auto& tok: tokens)
            //{
            //    cout << tok << endl;
            //}
        }
        
        //cout << ctx.ret() << endl;
        
        //cout << line << endl;
    }

    return 0;
}

