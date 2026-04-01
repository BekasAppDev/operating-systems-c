#include "util.h"

int string_to_int(char *s)
{
    int res = 0;
    if (s == NULL)
        return 0;

    for (int i = 0; s[i] != '\0'; i++)
    {
        if (s[i] >= '0' && s[i] <= '9')
        {
            res = res * 10 + (s[i] - '0');
        }
    }
    return res;
}

void int_to_padded_string(int num, char *str, int pad_len)
{
    if (pad_len > 0)
        sprintf(str, "%0*d", pad_len, num);
    else
        sprintf(str, "%d", num);
}