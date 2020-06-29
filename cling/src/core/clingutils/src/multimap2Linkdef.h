// For backward compatibility only
#ifdef __CINT__
#include <multimap>
#else
#include <map>
#endif
#include <string>

#pragma create TClass std::multimap<long,int>;
#pragma create TClass std::multimap<long,long>;
#pragma create TClass std::multimap<long,double>;
#pragma create TClass std::multimap<long,void*>;
#pragma create TClass std::multimap<long,char*>;

#pragma create TClass std::multimap<double,int>;
#pragma create TClass std::multimap<double,long>;
#pragma create TClass std::multimap<double,double>;
#pragma create TClass std::multimap<double,void*>;
#pragma create TClass std::multimap<double,char*>;
