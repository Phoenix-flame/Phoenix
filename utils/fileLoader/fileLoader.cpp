#include "fileLoader.hpp"

FileLoader::FileLoader(std::string filepath){
    this->filepath = filepath;
    this->load();
}
crow::response FileLoader::getContent(){
    crow::response res(404);
    return res;
}


void FileLoader::load(){
    this->content = readFile(this->filepath);
    if (this->content == "") { this->isLoaded = false; }
    else { this->isLoaded = true;}
}

crow::response ImageLoader::getContent(){
    if (!isLoaded) { this->load(); }
    crow::response res;
    res.body = content;
    res.set_header("Content-Type", "image/" + file_type);
    res.set_header("Content-Length", std::to_string(content.length()));
    return res;
}

crow::response TextLoader::getContent(){
    if (!isLoaded) { this->load(); }
    crow::response res;
    res.body = content;
    res.set_header("Content-Type", "text/" + file_type);
    res.set_header("Content-Length", std::to_string(content.length()));
    return res;
}
