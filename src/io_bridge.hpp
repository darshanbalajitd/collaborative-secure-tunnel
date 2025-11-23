#pragma once

#include <string>

class TLSWrapper;

void run_server_shell(TLSWrapper& tls, bool mirror_output, bool mirror_input, bool mirror_clean);
void run_client_console(TLSWrapper& tls);
