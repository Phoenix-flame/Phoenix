#include "utilities.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

char easyToLowerCase(char in) {
  if (in <= 'Z' && in >= 'A')
    return in - ('Z' - 'z');
  return in;
}

string toLowerCase(string s) {
  transform(s.begin(), s.end(), s.begin(), easyToLowerCase);
  return s;
}

bool comp::operator()(const string &lhs, const string &rhs) const {
  return toLowerCase(lhs) < toLowerCase(rhs);
}

string readFile(const char *filename) {
  string s = "";
  char buffer[BUFFER_SIZE];
  ifstream infile(filename);
  infile.seekg(0, infile.end);
  size_t length = infile.tellg();
  infile.seekg(0, infile.beg);
  if (length > sizeof(buffer))
    length = sizeof(buffer);

  infile.read(buffer, length);
  for (size_t i = 0; i < length; i++) {
    s += buffer[i];
  }
  return s;
}

string readFile(string filename) { return readFile(filename.c_str()); }

vector<string> split(string s, string delimiter, bool trim) {
  vector<string> tokens;
  if (trim)
    s.erase(remove(s.begin(), s.end(), ' '), s.end());
  size_t pos = 0;
  string token;
  while ((pos = s.find(delimiter)) != string::npos) {
    token = s.substr(0, pos);
    tokens.push_back(token);
    s.erase(0, pos + delimiter.length());
  }
  tokens.push_back(s);
  return tokens;
}
std::pair<std::string, std::string> splitKey(std::string s, std::string delimiter, bool trim) {
  std::pair<std::string, std::string> tokens;

  if (trim)
    s.erase(remove(s.begin(), s.end(), ' '), s.end());
  size_t pos = 0;
  string token;
  while ((pos = s.find(delimiter)) != string::npos) {
    token = s.substr(0, pos);
    tokens.first = plus2space(token);
    s.erase(0, pos + delimiter.length());
  }
  tokens.second = plus2space(s);
  return tokens;
}

void printVector(vector<string> v) {
  for (string s : v)
    cout << s << endl;
}

string urlEncode(string const &str) {
  char encode_buf[4];
  string result;
  encode_buf[0] = '%';
  result.reserve(str.size());

  // character selection for this algorithm is based on the following url:
  // http://www.blooberry.com/indexdot/html/topics/urlencoding.htm

  for (size_t pos = 0; pos < str.size(); ++pos) {
    switch (str[pos]) {
    default:
      if (str[pos] >= 32 && str[pos] < 127) {
        // character does not need to be escaped
        result += str[pos];
        break;
      }
      // else pass through to next case
    case '$':
    case '&':
    case '+':
    case ',':
    case '/':
    case ':':
    case ';':
    case '=':
    case '?':
    case '@':
    case '"':
    case '<':
    case '>':
    case '#':
    case '%':
    case '{':
    case '}':
    case '|':
    case '\\':
    case '^':
    case '~':
    case '[':
    case ']':
    case '`':
      // the character needs to be encoded
      sprintf(encode_buf + 1, "%02X", str[pos]);
      result += encode_buf;
      break;
    }
  };
  return result;
}

string urlDecode(string const &str) {
  char decode_buf[3];
  string result;
  result.reserve(str.size());

  for (size_t pos = 0; pos < str.size(); ++pos) {
    switch (str[pos]) {
    case '+':
      // convert to space character
      result += ' ';
      break;
    case '%':
      // decode hexidecimal value
      if (pos + 2 < str.size()) {
        decode_buf[0] = str[++pos];
        decode_buf[1] = str[++pos];
        decode_buf[2] = '\0';
        result += static_cast<char>(strtol(decode_buf, nullptr, 16));
      } else {
        // recover from error by not decoding character
        result += '%';
      }
      break;
    default:
      // character does not need to be escaped
      result += str[pos];
    }
  }
  return result;
}

string getExtension(string filePath) {
  size_t pos = filePath.find_last_of(".");
  return filePath.substr(pos != string::npos ? pos + 1 : filePath.size());
}

vector<string> tokenize(const string &cnt, char delimiter) {
  vector<string> res;
  istringstream is(cnt);
  string part;
  while (getline(is, part, delimiter))
    res.push_back(part);
  return res;
}

void replaceAll(std::string &str, const std::string &from,
                const std::string &to) {
  if (from.empty())
    return;
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length(); // In case 'to' contains 'from', like replacing
                              // 'x' with 'yx'
  }
}

int findSubStrPosition(std::string &str, std::string const &subStr,
                       int const &pos) {
  size_t found = str.find(subStr, pos);
  if (found == string::npos)
    return -1;
  return found;
}

int writeObjectToFile(const char *object, int size,
                      std::string const &filePath) {
  ofstream file;
  file.open(filePath, fstream::out);
  if (!file.is_open())
    return -1;
  file.write(object, size);
  file.close();
  return sizeof(object);
}

int writeToFile(std::string const &str, std::string const &filePath) {
  return writeObjectToFile(str.c_str(), str.length(), filePath);
}

cimap getCimapFromString(std::string str) {
  cimap m;
  vector<string> tokenized = tokenize(str, '&');
  for (auto token : tokenized) {
    vector<string> keyValue = tokenize(token, '=');
    if (keyValue.size() != 2)
      continue;
    string key = keyValue[0];
    string value = keyValue[1];
    m[key] = value;
  }
  return m;
}
int readMapFromFile(std::string fname, std::map<std::string, std::string> *m) {
  int count = 0;

  FILE *fp = fopen(fname.c_str(), "r");
  if (!fp)
    return -errno;

  m->clear();

  char *buf = 0;
  size_t buflen = 0;

  while (getline(&buf, &buflen, fp) > 0) {
    char *nl = strchr(buf, '\n');
    if (nl == NULL)
      continue;
    *nl = 0;

    char *sep = strchr(buf, '=');
    if (sep == NULL)
      continue;
    *sep = 0;
    sep++;

    std::string s1 = buf;
    std::string s2 = sep;

    (*m)[s1] = s2;

    count++;
  }

  if (buf)
    free(buf);

  fclose(fp);
  return count;
}


std::string getRandomSessionID(){
    srand(time(NULL) + rand());
    std::stringstream ss;
    static const char alphanum[] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";

    for (int i = 0; i < 64; ++i) {
        ss << alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    return ss.str();
}


int to_int(std::string input){
    std::stringstream buf(input);
    int x = 0; 
    buf >> x;
    return x;
}

std::string space2underscore(std::string text) {
    for(std::string::iterator it = text.begin(); it != text.end(); ++it) {
        if(*it == ' ') {
            *it = '_';
        }
    }
    return text;
}

std::string plus2space(std::string text) {
    for(std::string::iterator it = text.begin(); it != text.end(); ++it) {
        if(*it == '+') {
            *it = ' ';
        }
    }
    return text;
}

bool is_base64(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string base64_decode(std::string const& encoded_string) {\
    const std::string base64_chars = 
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";
    int in_len = encoded_string.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i ==4) {
        for (i = 0; i <4; i++)
            char_array_4[i] = base64_chars.find(char_array_4[i]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (i = 0; (i < 3); i++)
            ret += char_array_3[i];
        i = 0;
        }
    }

    if (i) {
        for (j = i; j <4; j++)
        char_array_4[j] = 0;

        for (j = 0; j <4; j++)
        char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
    }

    return ret;
}


std::map<std::string, std::string> processReq(std::string body){
    std::map<std::string, std::string> res;
    std::vector<std::string> data = split(body, "&");
    for (auto i:data){
        std::pair<std::string, std::string> val = splitKey(i, "=");
        res.insert(val);

    }
    return res;
}


void createFile(std::string path, std::string content){
    std::ofstream outfile;
    outfile.open(path, std::ios::out|std::ios::binary);
    outfile << content;
    outfile.close();
}


bool file_exist (const std::string& name) {
    return ( access( name.c_str(), F_OK ) != -1 );
}

