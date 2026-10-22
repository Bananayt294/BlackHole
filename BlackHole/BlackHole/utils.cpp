#include "utils.h"

std::string readTextFile(const std::string &filepath) {
	std::ifstream infile(filepath);
	if (infile.is_open()) {
		std::stringstream buffer;
		buffer << infile.rdbuf();
		const std::string output = buffer.str();
		infile.close();
		return output;
	};
	return std::string();
}