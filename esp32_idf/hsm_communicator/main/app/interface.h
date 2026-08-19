#ifndef INTERFACE_H
#define INTERFACE_H

#include <string>

void printStatus();
void handleCommand(const std::string& cmd);
void commandTask(void *arg);

#endif