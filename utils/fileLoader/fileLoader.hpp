#ifndef __FILE__LOADER__HPP
#define __FILE__LOADER__HPP

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include "../../server/crow_all.h"
#include "../utilities.hpp"

class FileLoader{
public:
    FileLoader(std::string filepath);
    virtual crow::response getContent();
protected:
    std::string content;
    std::string filepath;
    std::string file_type;
    bool isLoaded = false;

    void load();
};

class ImageLoader: public  FileLoader{
public:
    ImageLoader(std::string filepath): FileLoader(filepath){
        file_type = getExtension(filepath);
    }
    crow::response getContent() override;
};

class TextLoader: public  FileLoader{
public:
    TextLoader(std::string filepath): FileLoader(filepath){
        file_type = getExtension(filepath);
    }
    crow::response getContent() override;
};


#endif