#ifndef __MULTIPARTPARSER__
#define __MULTIPARTPARSER__
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>


enum MP_ERROR{
    SIZE_ERROR,
    TYPE_ERROR,
    CONTENT_ERROR
};


class MultipartParser{
public:
    MultipartParser();
    MultipartParser(std::string content);
    void setContent(std::string content);
    MP_ERROR saveFile();
    std::string getContent();

private:
    std::pair<std::string, std::string> parse();
    std::string content = "";
};


#endif