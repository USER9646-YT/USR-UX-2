// 64-bit division helpers that newer gcc emits (and tcc's libtcc1 lacks).
// They use shift-and-subtract only: a / or % on long long here could be turned
// by gcc back into a call to these very functions.

unsigned long long __udivmoddi4(unsigned long long n, unsigned long long d, unsigned long long *rem) {
    unsigned long long q = 0, r = 0;

    if (d == 0) {                                   // trap like the hardware does
        volatile int zero = 0;
        q = (unsigned long long)(1 / zero);
    }
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= 1ULL << i;
        }
    }
    if (rem) *rem = r;
    return q;
}

long long __divmoddi4(long long a, long long b, long long *rem) {
    int neg_a = a < 0, neg_b = b < 0;
    unsigned long long ua = neg_a ? 0ULL - (unsigned long long)a : (unsigned long long)a;
    unsigned long long ub = neg_b ? 0ULL - (unsigned long long)b : (unsigned long long)b;
    unsigned long long ur;
    unsigned long long uq = __udivmoddi4(ua, ub, &ur);

    if (rem) *rem = neg_a ? (long long)(0ULL - ur) : (long long)ur;     // remainder takes the dividend's sign
    return (neg_a != neg_b) ? (long long)(0ULL - uq) : (long long)uq;
}