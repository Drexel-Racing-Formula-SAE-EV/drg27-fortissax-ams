#include <ams_core/ams_core_contract.h>

#include <stdio.h>

int main(void)
{
    int ret = ams_core_contract_check();

    if (ret != 0) {
        fprintf(stderr, "FAIL: ams_core null-platform contract returned %d\n", ret);
        return 1;
    }

    puts("PASS: ams_core null-platform smoke");
    return 0;
}
