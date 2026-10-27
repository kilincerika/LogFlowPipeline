 #ifndef LOG_RECORD_HPP
#define LOG_RECORD_HPP

#include <string>
#include <map>

struct LogRecord {
    std::string timestamp;
    std::string clientIp;
    int status = 0;
    std::string path;
    
    // Projeksiyon (alan filtreleme) için anahtar-deðer haritasý
    std::map<std::string, std::string> fields;

    void syncMap() {
        fields["timestamp"] = timestamp;
        fields["clientIp"] = clientIp;
        fields["status"] = std::to_string(status);
        fields["path"] = path;
    }
};

#endif // LOG_RECORD_HPP
