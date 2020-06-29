// For backward compatibility only
#ifdef __CINT__
#include <multiset>
#else
#include <set>
#endif
#include <string>

#pragma create TClass std::multiset<int>;
#pragma create TClass std::multiset<long>;
#pragma create TClass std::multiset<float>;
#pragma create TClass std::multiset<double>;
#pragma create TClass std::multiset<void*>;
#pragma create TClass std::multiset<char*>;
#pragma create TClass std::multiset<std::string>;
