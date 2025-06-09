#include <string>
#include <sstream>
#include <unordered_map>

class PriorityStateClass
{
public:
    std::string get_marking_string() const
    {
        std::stringstream ss;
        ss << "{";
        bool first = true;
        for (const auto &[place, tokens] : marking)
        {
            if (first)
                first = false;
            else
                ss << ", ";
            ss << "P" << place << ":" << tokens;
        }
        ss << "}";
        return ss.str();
    }

private:
    // Assuming marking is a map from place to tokens
    std::unordered_map<int, int> marking;
};
