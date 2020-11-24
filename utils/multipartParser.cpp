#include "multipartParser.hpp"


MultipartParser::MultipartParser(){

}
MultipartParser::MultipartParser(std::string content){
    this->content = content;
}

MP_ERROR MultipartParser::saveFile(){
    if (this->content == ""){
        return CONTENT_ERROR;
    }
    std::pair<std::string, std::string> res = this->parse();
    
    std::ofstream outfile;
    outfile.open(res.first, std::ios::out|std::ios::binary);
    outfile << res.second;
    outfile.close();
}

std::string MultipartParser::getContent(){
    if (this->content == ""){
        return "";
    }
    std::pair<std::string, std::string> res = this->parse();
    return res.second;
}

std::pair<std::string, std::string> MultipartParser::parse(){
    std::istringstream iss(this->content);
    std::string line;
    std::string data;
    std::string filename;
    int idx = 0;
    while (std::getline(iss, line)){
        idx ++;
        if (idx == 2){
            int idx_filename = line.find("filename=\"");
            for (unsigned int j = idx_filename + 10 ; j < line.size() ; j++){
                if (line[j] == '\"'){
                    break;
                }
                filename += line[j];
            }
        }
        if (idx <= 4){ continue;}
        if (line.find("------WebKitFormBoundary")!= -1) { break;}
        data += line;
        data += "\n";
    }


    data.pop_back();
    return std::pair<std::string, std::string>(filename, data);
}

void MultipartParser::setContent(std::string content){
    this->content = content;
}