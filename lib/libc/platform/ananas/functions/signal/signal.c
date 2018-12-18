#include <signal.h>
#include <string.h>

sig_t signal(int sig, sig_t func)
{
    struct sigaction act;
    memset(&act, 0, sizeof(act));

    act.sa_handler = func;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    struct sigaction oact;
    if (sigaction(sig, &act, &oact) < 0)
        return SIG_ERR;
    return oact.sa_handler;
}
