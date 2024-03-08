#include <iostream>
#include <vector>

std::vector<std::string> split(std::string& s, const std::string &d) {
    std::vector<std::string> res;
    size_t start = 0;
    size_t end = s.find(d);
    // 以d为分割符将s分割成多个字符串，并将这些字符串添加到res中
    while (end != std::string::npos) {
        res.push_back(s.substr(start, end - start));
        start = end + d.size();
        end = s.find(d, start);
    }
    start = s.find_last_of(d); // 再将剩余部分添加到res中
    res.push_back(s.substr(start + d.size(),s.size() - 1 - start));
    return res;
}

int main()
{
    std::string s = "1,2,30";
    std::string d = ",";
    std::vector<std::string> res = split(s, d);
    for (std::string str : res) {
        std::cout << str << std::endl;
    }
    return 0;
}