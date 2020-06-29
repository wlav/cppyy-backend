// For backward compatibility only
#ifdef __CINT__
#include <multimap>
#else
#include <map>
#endif
#include <string>

#pragma create TClass std::multimap<char*,int>;
#pragma create TClass std::multimap<char*,long>;
#pragma create TClass std::multimap<char*,double>;
#pragma create TClass std::multimap<char*,void*>;
#pragma create TClass std::multimap<char*,char*>;

#pragma create TClass std::multimap<std::string,int>;
#pragma create TClass std::multimap<std::string,long>;
#pragma create TClass std::multimap<std::string,double>;
#pragma create TClass std::multimap<std::string,void*>;

