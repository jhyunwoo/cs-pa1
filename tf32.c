#include <stdio.h>
#include "tf32.h"

/*
    Do NOT include any C libraries or header files
    except those above
    
    use the defines in tf32.h
    if neccessary, you can add some macros below
*/

// tf32 형식을 구현하기 위해 19비트를 사용함
// 부호 1비트, 지수 8비트, 가수 10비트
#define TF32_SIGN_MASK 0x40000u // 부호 비트 마스크
#define TF32_EXP_MASK 0x3FC00u // 지수 비트 마스크
#define TF32_FRAC_MASK 0x003FFu // 가수 비트 마스크
#define TF32_EXP_BIAS 127 // 지수의 바이어스 값
#define TF32_EXP_SHIFT 10 // 지수 비트를 제자리로 옮기기 위한 시프트 값
#define TF32_FRAC_BITS 10 // 가수의 비트 수

// 32비트 정수를 tf32 형식으로 변환하는 함수
tf32 int2tf32(int in) {
    // 입력 값이 0인 경우, 부동소수점 0으로 변환하여 반환
    if(in==0){
        return 0;
    }

    unsigned int sign = 0; // 부호 비트를 저장할 변수
    unsigned int abs_val; //입력값의 절댓값을 저장할 변수

    // 입력값의 부호를 확인하고 절댓값을 계산
    if(in<0){
        sign = 1; // 음수일 경우 부호 비트를 1로 설정
        if(in==0x80000000){
            abs_val = 0x80000000u;
        } else {
            abs_val = (unsigned int)(-in);
        }
    } else {
        abs_val = (unsigned int)in;
    }

    // 절대값에서 가장 왼쪽에 있는 1 비트(MSB)의 위치를 찾음
    int leading_bit = 31;
    // 루프를 돌며 MSB를 찾음
    while(leading_bit >= 0 && !((abs_val >> leading_bit) & 1)){
        leading_bit--;
    }

    // 만약 leading_bit이 0보다 작으면 abs_val이 0이라는 의미
    if(leading_bit < 0){
        return 0;
    }

    // 지수 계산
    int exp = leading_bit + TF32_EXP_BIAS; // 바이어스를 더해줌

    // 가수 계산 및 반올림 처리
    unsigned int frac;
    // MSB 위치가 가수 비트 수 보다 크거나 같으면 오른쪽으로 시프트하여 가수를 구함
    if(leading_bit >= TF32_FRAC_BITS){
        int shift = leading_bit - TF32_FRAC_BITS;
        frac = (abs_val >> shift) & TF32_FRAC_MASK;

        // 가장 가까운 짝수로 반올림 처리
        if(shift > 0){
            // 시프트로 인해 잘려진 나버지 미트들을 구함
            unsigned int remainder = abs_val & ((1u << shift) - 1);
            // 중간값을 계산
            unsigned int half = 1u << (shift - 1);
            // 나머지가 중간값보다 크거나, 중간값과 같으면서 가수의 마지막 비트가 1이면 반올림
            if(remainder > half || (remainder==half && (frac & 1))){
                frac++;
                // 반올림으로 인해 가수가 10비트를 초과하면
                if(frac > TF32_FRAC_MASK){
                    frac = 0; // 가수를 0으로 설정
                    exp++; // 지수 1 증가
                }
            }
        }
    }else{
        // MSB 위치가 가수 비트 수 보다 작으면, 왼쪽으로 시프트하여 가수를 구함
        frac = (abs_val << (TF32_FRAC_BITS - leading_bit)) & TF32_FRAC_MASK;
    }

    // 부호, 지수, 가수를 19비트 tf32 형식으로 조합 후 반환
    tf32 result = (sign << 18) | (exp << TF32_EXP_SHIFT) | frac;

    return result;
}

// tf32 형식을 정수로 변환하는 함수
int tf322int(tf32 in) {
    // 입력된 tf32 값에서 부호, 지수, 가수를 추출
    unsigned int sign = (in >> 18) & 1;
    unsigned int exp = (in >> TF32_EXP_SHIFT) & 0xFF;
    unsigned int frac = in & TF32_FRAC_MASK;

    // 지수가 모두 1인 경우 무한대 또는 NaN이기 때문에 따로 처리
    if(exp == 0xFF){
        // 가수가 0이면 무한대, 0이 아니면 NaN
        return (frac == 0) ? (sign ? 0x80000000 : 0x7FFFFFFF) : 0x80000000;
    }

    // 지수가 0인 경우 (0 또는 비정규화된 수)
    if(exp == 0){
        // 모두 1보다 작은 값이기 때문에 0을 반환
        return 0;
    }

    // 실제 지수 계산
    int true_exp = (int)exp - TF32_EXP_BIAS;

    // 만약 실제 지수가 0보다 작으면 1보다 작은 값 -> 0으로 반환
    if(true_exp < 0){
        return 0;
    }


    // 가수 구하기
    unsigned int mantissa = (1u << TF32_FRAC_BITS) | frac;
    unsigned int result_unit; // 부호가 없는 정수 결과를 저장할 변수

    if (true_exp >= TF32_FRAC_BITS) {
        int lshift = true_exp - TF32_FRAC_BITS;
        if(lshift > 21){
            return sign ? 0x80000000 : 0x7FFFFFFF;
        }
        result_unit = mantissa << lshift;
    } else {
        int rshift = TF32_FRAC_BITS - true_exp;
        unsigned int main = mantissa >> rshift;
        unsigned int round_bit = (mantissa >> (rshift - 1)) & 1u;
        unsigned int sticky = (rshift > 1) ? (mantissa & ((1u<<(rshift-1)) - 1u)) != 0 : 0;

        if(round_bit && (sticky || (main & 1u))){
            main ++;
        }
        result_unit = main;
    }

    if(!sign){
        return (result_unit > 0x7FFFFFFFu) ? 0x7FFFFFFF : (int)result_unit;
    }

    if(result_unit >= 0x80000000u) {
        return 0x80000000;
    }

    return -(int)result_unit;
}

// double 타입을 tf32으로 변환하는 함수
tf32 double2tf32(double in) {
    // union을 사용하여 double 값의 비트 표현에 직접 접근
    union {double f; unsigned long long u;} x;
    x.f = in;

    unsigned long long bits = x.u; // double의 64비트 표현형을 가져옴
    // double의 비트 표현에서 부호, 지수, 가수를 추출
    unsigned long long sign = (bits >> 63) & 1;
    unsigned long long exp = (bits >> 52) & 0x7FF;
    unsigned long long frac = bits & 0xFFFFFFFFFFFFFull;

    // 지수가 모두 1인 경우 무한대 또는 NaN
    if(exp == 0x7FF){
        if(frac == 0){ // 가수가 0이면 무한대
            return (sign << 18) | 0x3FC00; // tf32의 무한대 값으로 변환
        }else { // 가수가 0이 아니면 NaN
            return 0x3FE00; // tf32의 NaN 값을 반환
        }
    }

    // 입력값이 0인 경우
    if(exp == 0 && frac == 0){
        return sign << 18; // 부호 비트만 설정된 0을 반환
    }

    // 정규화 과정
    int double_exp;
    
    if(exp == 0){ // double이 비정규화된 수인 경우
        double_exp = 1 - 1023; // 비정규화 수의 지수는 1 - bias
        // 암시적 1이 1이 될 때까지 가수를 왼쪽으로 시프트하고 지수를 감소
        while((frac & 0x10000000000000ull)==0){
            frac <<=1;
            double_exp--;
        }
        frac &= 0xFFFFFFFFFFFFFull; // 정규화 후 암시적 1을 제거함
    }else{ // double이 정규화된 수인 경우
        double_exp = (int)exp - 1023; // 실제 지수 계산
    }

    // 암시적 1
    unsigned long long mant = (1ull << 52) | frac;
    // doubel의 실제 지수를 tf32의 bias된 지수로 변환
    int tf32_exp = double_exp + TF32_EXP_BIAS;

    // 오버플로우 처리
    if(tf32_exp >= 0xFF){
        return (sign << 18) | 0x3FC00;
    }

    // 언더플로우 및 비정규화 처리
    if(tf32_exp <= 0){
        int shift_to_10 = 52 - TF32_FRAC_BITS;
        unsigned long long m10 = mant >> shift_to_10;
        int extra = 1 - tf32_exp;
        unsigned long long total_shift = shift_to_10 + extra;
        if(total_shift >= 64){
            return (sign << 18);
        }

        unsigned long long frac11 = mant >> total_shift;
        unsigned long long half = 1ull << (total_shift - 1);
        unsigned long long rem = mant & ((1ull << total_shift) - 1ull);

        unsigned int frac10 = (unsigned int)(frac11 & TF32_FRAC_MASK);
        unsigned long long lsb = frac10 & 1u;
        if(rem > half || (rem == half && lsb)){
            frac10++;
            if (frac10 > TF32_FRAC_MASK) {
                return (sign << 18) | (1u << TF32_EXP_SHIFT);
            }
        }
        return (sign << 18) | frac10;
    }

    // 정규화 된 수 처리
    int shift = 52 - TF32_FRAC_BITS; // 시프트 양
    unsigned long long frac10 = (mant >> shift) & TF32_FRAC_MASK;
    unsigned long long rem = mant & ((1ull << shift) - 1ull);
    unsigned long long half = 1ull << (shift - 1);
    if(rem > half || (rem == half && (frac10 & 1))){
        frac10++;
        if(frac10 > TF32_FRAC_MASK){
            frac10 = 0;
            tf32_exp++;
            if(tf32_exp>=0xFF){
                return (sign << 18) | 0x3FC00;
            }
        }
    }
    return (sign << 18) | (tf32_exp << TF32_EXP_SHIFT) | (unsigned)frac10;
}

// tf32 형식을 double 타입으로 변환하는 함수
double tf322double(tf32 in) {
    // tf32에서 부호, 지수, 가수를 추출함
    unsigned int sign = (in >> 18) & 1;
    unsigned int exp = (in >> TF32_EXP_SHIFT) & 0xFF;
    unsigned int frac = in & TF32_FRAC_MASK;

    // union을 사용하여 double 비트 표현을 조합
    union {double f; unsigned long long u;} result;

    // 지수가 모두 1인 경우 무한대 또는 NaN으로 처리
    if(exp == 0xFF){
        if(frac == 0){ // 무한대
            result.u = ((unsigned long long)sign << 63) | 0x7FF0000000000000ull;
        }else { // NaN
            result.u = 0x7FF8000000000000ull;
        }
        return result.f;
    }

    // 지수가 0인 경우
    if(exp == 0){
        if(frac == 0){
            result.u = (unsigned long long)sign << 63; // 부호 있는 0으로 설정
            return result.f;
        }
        // 비정규화 된 수를 처리
        unsigned int t = frac;
        unsigned int k = 0;
        while(t >> 1){
            t >>=1;
            k++;
        }
        unsigned int r = frac - (1u << k);

        int E = 887 + (int)k; 
        unsigned long long dfrac = (unsigned long long) r << (52 - k);
    
        result.u = ((unsigned long long)sign << 63) | ((unsigned long long)E << 52) | dfrac;
        return result.f;
    }

    // 정규화된 수 처리
    int true_exp = (int)exp - TF32_EXP_BIAS; // tf32의 실제 지수를 계산
    int E = true_exp + 1023; // double 바이어스 더하기

    // 최종 결과 조합
    unsigned long long double_frac = (unsigned long long)frac << (52 - TF32_FRAC_BITS);
    result.u = ((unsigned long long)sign << 63) | ((unsigned long long)E << 52) | double_frac;
    // 최종 결과 반환
    return result.f;
}

// tf32 타입의 수를 더하는 함수
tf32 tf32_add(tf32 a, tf32 b) {
    // 두 수에서 부호, 지수, 가수를 추출함
    unsigned int sign_a = (a>>18) & 1;
    unsigned int exp_a = (a >> TF32_EXP_SHIFT) & 0xFF;
    unsigned int frac_a = a & TF32_FRAC_MASK;
    unsigned int sign_b = (b >> 18) & 1;
    unsigned int exp_b = (b >> TF32_EXP_SHIFT) & 0xFF;
    unsigned int frac_b = b & TF32_FRAC_MASK;

    // a가 무한대 또는 NaN인 경우 처리
    if(exp_a == 0xFF){
        if(frac_a != 0){ // a가 NaN이면 NaN 반환
            return 0x3FE00;
        }
        // Inf + (-Inf)는 NaN 반환
        if(exp_b == 0xFF && frac_b == 0 && sign_a != sign_b){
            return 0x3FE00;
        }
        return a;
    }

    // b가 무한대 또는 NaN인 경우
    if(exp_b == 0xFF){
        if(frac_b != 0){ // b가 NaN이면 NaN 반환
            return 0x3FE00;
        }
        return b;
    }

    // 한쪽이 0이면 다른 쪽 값을 바로 반환
    if(exp_a == 0 && frac_a == 0){
        return b;
    }
    if(exp_b == 0 && frac_b == 0){
        return a;
    }

    // 지수가 더 큰 쪽을 a로 오도록 정렬
    if(exp_b > exp_a || (exp_b == exp_a && frac_b > frac_a)){
        // 각 수의 데이터를 교환
        unsigned int tmp_s = sign_a;
        sign_a = sign_b;
        sign_b = tmp_s;
        unsigned int tmp_e = exp_a;
        exp_a = exp_b;
        exp_b = tmp_e;
        unsigned int tmp_f = frac_a;
        frac_a = frac_b;
        frac_b = tmp_f; 
    }

    // 가수를 복원
    const unsigned HIDDEN = (1u << TF32_FRAC_BITS);
    unsigned int mant_a = (exp_a ? (HIDDEN | frac_a): frac_a);
    unsigned int mant_b = (exp_b ? (HIDDEN | frac_b):frac_b);

    // 비정규화 수의 지수를 1로 간주하여 게산
    int true_exp_a = (exp_a ? exp_a : 1);
    int true_exp_b = (exp_b ? exp_b:1);

    // GRS 비트 확보 후 정렬
    const unsigned GRS = 3;
    unsigned int mant_a_ext = mant_a << GRS;
    unsigned int mant_b_ext = mant_b << GRS;

    // 지수 차이 계산
    int exp_diff = true_exp_a - true_exp_b;
    if(exp_diff > 0){
        if(exp_diff >= 32){
            // 전부 날라가면 sticky를 1로 설정
            mant_b_ext = (mant_b_ext ? 1u : 0u);
        }else {
            unsigned int lost_mask = (1u << exp_diff) - 1u;
            unsigned int lost = mant_b_ext & lost_mask;
            mant_b_ext = (mant_b_ext >> exp_diff) | (lost ? 1u : 0u); // sticky 반영
        }
    }

    unsigned int res_sign = sign_a;
    unsigned int res_mant_ext;
    int res_exp = true_exp_a;


    // 부호에 따라 덧셈 또는 뺄셈 수행
    if(sign_a == sign_b){ // 부호가 같으면 덧셈
        res_mant_ext = mant_a_ext + mant_b_ext;
        // 오버플로우
        if(res_mant_ext & (1u << (TF32_FRAC_BITS + GRS + 1))){
            res_mant_ext >>= 1;
            res_exp++;
        }
    }else { // 부호가 다른 경우 뺄셈
        if(mant_a_ext >= mant_b_ext){
            res_mant_ext = mant_a_ext - mant_b_ext;
        }else {
            res_mant_ext = mant_b_ext - mant_a_ext;
            res_sign = sign_b;
        }

        if(res_mant_ext == 0){
            return 0;
        }

        // leading 1을 FRAC_BIT 위치로 이동
        while(res_exp > 1 && res_mant_ext < (1u << (TF32_FRAC_BITS + GRS))){
            res_mant_ext <<= 1;
            res_exp--;
        }
    }

    // 최종 지수가 오버플로우 될 경우 무한대 반환
    if(res_exp >= 0xFF){
        return (res_sign << 18) | 0x3FC00;
    }

    // 언더플로우 처리
    if (res_exp <= 0) {
        unsigned shift = (unsigned)(1 - res_exp);
        // sticky 포함한 시프트
        unsigned int lost = (shift >= 32) ? (res_mant_ext != 0)
                                        : ((res_mant_ext & ((1u<<shift)-1u)) != 0);
        res_mant_ext = (shift >= 32) ? 0u : (res_mant_ext >> shift);
        if (lost) res_mant_ext |= 1u; // sticky=1

        unsigned int frac_field = (res_mant_ext >> GRS) & TF32_FRAC_MASK;
        unsigned int guard = (res_mant_ext >> 2) & 1u;
        unsigned int roundb = (res_mant_ext >> 1) & 1u;
        unsigned int sticky = res_mant_ext & 1u;

        if (guard && (roundb | sticky | (frac_field & 1u))) {
            frac_field++;
            if (frac_field > TF32_FRAC_MASK) { frac_field = 0; /* 여긴 exp=0 유지 */ }
        }
        return (res_sign << 18) | /*exp=0*/ (0u << TF32_EXP_SHIFT) | frac_field;
    }

    // GRS 반올림
    unsigned int frac_field = (res_mant_ext >> GRS) & TF32_FRAC_MASK; // 암시적 1을 제거
    unsigned int guard = (res_mant_ext >> 2) & 1u;
    unsigned int roundb = (res_mant_ext >> 1) & 1u;
    unsigned int sticky = res_mant_ext & 1u;

    if(guard && (roundb|sticky | (frac_field & 1u))){
        frac_field++;
        if(frac_field > TF32_FRAC_MASK){
            // 반올림 캐리로 정규화를 한 번 더 수행
            frac_field = 0;
            res_exp++;
            if(res_exp >= 0xFF){
                return (res_sign << 18) | 0x3FC00;
            }
        }
    }

    // 최종 결과 반환
    return (res_sign << 18) | (res_exp << TF32_EXP_SHIFT) | frac_field;
}

// 두 tf32 값을 곱하는 함수
tf32 tf32_mul(tf32 a, tf32 b) {
    // 두 수에서 부호, 지수, 가수를 추출함
    unsigned int sign_a = (a >> 18) & 1;
    unsigned int exp_a = (a >> TF32_EXP_SHIFT) & 0xFF;
    unsigned int frac_a = a & TF32_FRAC_MASK;
    unsigned int sign_b = (b >> 18) & 1;
    unsigned int exp_b = (b >> TF32_EXP_SHIFT) & 0xFF;
    unsigned int frac_b = b & TF32_FRAC_MASK;

    // 결과의 부흐는 두 부호의 XOR 연산을 통해 결정
    unsigned int result_sign = sign_a ^ sign_b;

    // a가 무한대 또는 NaN인 경우
    if(exp_a == 0xFF){
        if(frac_a != 0){ // a가 NaN인 경우 NaN 반환
            return 0x3FE00;
        }
        if(exp_b == 0 && frac_b == 0){ // 무한대인 경우 무한대 반환
            return 0x3FE00;
        }
        return (result_sign << 18) | 0x3FC00; // Inf를 곱할 경우 Inf 반환
    }

    if(exp_b == 0xFF){ // b가 무한대 또는 NaN인 경우
        if(frac_b != 0){ // b가 NaN인 경우 NaN 반환
            return 0x3FE00;
        }
        if(exp_a == 0 && frac_a == 0){ // 무한대랑 0을 곱할 경우 NaN 반환
            return 0x3FE00;
        }
        return (result_sign << 18) | 0x3FC00; // 무한대랑 곱할 경우 무한대 반환
    }

    // 둘 중 하나라도 0이면 결과는 0
    if((exp_a == 0 && frac_a == 0) || (exp_b == 0 && frac_b == 0)){
        return result_sign << 18; // 부호 있는 0을 반환
    }

    // 가수 복원을 위해 암시적 1을 추가
    unsigned int mant_a = (exp_a == 0) ? frac_a : ((1u << TF32_FRAC_BITS) | frac_a);
    unsigned int mant_b = (exp_b == 0) ? frac_b : ((1u<<TF32_FRAC_BITS) | frac_b);

    // 비정규화 수의 지수를 1로 간주하여 처리함
    int true_exp_a = (exp_a == 0) ? 1 : exp_a;
    int true_exp_b = (exp_b == 0) ? 1 : exp_b;

    // 가수 곱셈 처리
    unsigned int product = mant_a * mant_b;

    // 지수 계산
    int result_exp = (true_exp_a - TF32_EXP_BIAS) + (true_exp_b - TF32_EXP_BIAS) + TF32_EXP_BIAS;

    // 곱셈 결과 정규화
    int shift = 0;

    // 곱셈 결과가 22비트이면 정규화를 위해 오른쪽으로 시프트
    if(product & (1u << (TF32_FRAC_BITS * 2 + 1))){
        shift = TF32_FRAC_BITS + 1;
        result_exp++; // 지수 증가
    }else { // 결과가 21비트일 경우
        shift = TF32_FRAC_BITS;
    }

    // 정규화된 곱셈 결과에서 상위 10비트를 가수로 추출
    unsigned int result_frac = (product >> shift) & TF32_FRAC_MASK;

    // 반올림 처리
    if(shift > 0){
        // 잘려나간 비트와 중간값을 이용
        unsigned int remainder = product & ((1u << shift) - 1);
        unsigned int half = 1u << (shift - 1);
        if(remainder > half || (remainder == half && (result_frac & 1))){
            result_frac++; // 반올림
            if(result_frac > TF32_FRAC_MASK){ // 가수 오버플로우 처리
                result_frac = 0;
                result_exp++;
            }
        }
    }

    // 지수 오버플로우 확인
    if(result_exp >= 0xFF){
        return (result_sign << 18) | 0x3FC00; // 오버플로우 발생할 경우 무한대 반환
    }

    // 지수 언드플로우 확인
    if(result_exp <=0){
        unsigned int total_shift = (unsigned)shift + (unsigned int)(1 - result_exp);
        if(total_shift >= 32){
            return (result_sign << 18);
        }

        unsigned int main = (product >> total_shift) & TF32_FRAC_MASK;
        unsigned int rem_mask = (1u << total_shift) - 1u;
        unsigned int rem = product & rem_mask;
        unsigned int half = 1u << (total_shift - 1);
        if(rem > half || (rem == half && (main & 1u))){
            main++;
            if(main > TF32_FRAC_MASK) {
                main = 0;
            }
        }
        return (result_sign << 18) | main;
    }

    // 최종 결과 조합 후 반환
    return (result_sign << 18) | (result_exp << TF32_EXP_SHIFT) | result_frac;
}

// 두 tf32 데이터를 나누는 함수
tf32 tf32_div(tf32 a, tf32 b) {
    // 두 수에서 부호, 지수, 가수를 추출
    unsigned int sign_a = (a >> 18) & 1;
    unsigned int exp_a = (a >> TF32_EXP_SHIFT) & 0xFF;
    unsigned int frac_a = a & TF32_FRAC_MASK;
    unsigned int sign_b = (b >> 18) & 1;
    unsigned int exp_b = (b >> TF32_EXP_SHIFT) & 0xFF;
    unsigned int frac_b = b & TF32_FRAC_MASK;

    // XOR 연산을 통해 결과 부호 결정
    unsigned int result_sign = sign_a ^ sign_b;

    // a가 무한대 또는 NaN인 경우
    if(exp_a == 0xFF){
        if(frac_a !=0){ // NaN인 경우 NaN 반환
            return 0x3FE00;
        }
        if(exp_b == 0xFF){ // 무한대를 무한대로 낮눌 경우 NaN 반환
            return 0x3FE00;
        }
        return (result_sign << 18) | 0x3FC00; // 무한대를 일반적인 수로 나눌 경우 무한대 반환
    }

    if(exp_b == 0xFF){ // b가 무한대 또는 NaN인 경우
        if(frac_b != 0){ // 일반적인 수를 NaN으로 나눌 경우 NaN을 반환
            return 0x3FE00;
        }
        return result_sign << 18; // 일반적인 수를 무한대로 나눌 경우 0을 반환
    }

    if(exp_b == 0 && frac_b == 0){ // 0으로 나눌 경우
        if(exp_a == 0 && frac_a == 0){ // 0을 0으로 나눌 경우 NaN 반환
            return 0x3FE00;
        }
        return (result_sign << 18) | 0x3FC00; // 0으로 나눈 경우 무한대 반환
    }

    // a가 0인경우 0 반환
    if(exp_a == 0 && frac_a == 0){
        return result_sign << 18;
    }

    // Newton-Raphson 방식을 활용하여 1/|b|를 계산 (근사)
    int iter = 10; // 반복 횟수

    // b의 절댓값
    tf32 b_abs = b & 0x3FFFF;

    // b의 실제 지수 값을 추정
    int exp_val = (exp_b == 0) ? -126 : ((int)exp_b - TF32_EXP_BIAS);

    // 1/b의 지수는 -exp_val이 됨
    int neg_exp = - exp_val;

    // 1/|b|의 초기 추정값 y를 구함. 
    tf32 y;
    int init_exp = neg_exp+TF32_EXP_BIAS; // y의 바이어스 된 지수
    if(init_exp <= 0){
        y = 0; // 언더플로우 처리
    } else if (init_exp >= 0xFF){
        y = 0x3FC00; // 오버플로우 처리
    } else {
        y = init_exp << TF32_EXP_SHIFT; // 가수는 0으로 두고 지수만 설정
    }

    // tf32로 표현된 2.0
    tf32 two = (128 << TF32_EXP_SHIFT); // 2.0 = 1.0 * 2^1,

    for(int i = 0; i < iter; i++){
        tf32 b_times_y = tf32_mul(b_abs, y);
        tf32 two_minus_by = tf32_add(two, b_times_y ^ 0x40000);
        y = tf32_mul(y, two_minus_by);
    }

    // a/b = a * (1/b)로 계산하여 최종 결과 도출
    tf32 result = tf32_mul(a, y);
    unsigned int e = (result >> 10) & 0xFF, f = result & 0x3FF;
    if (!(e == 0xFF && f != 0)) { // NaN이 아니면
        if (result_sign) {
            result |= 0x40000;
        }else {
            result &= 0x3FFFF;
        }
    }

    // 최종 결과 반환
    return result;
}

// 두 tf32 타입의 데이터를 비교하는 함수
int tf32_compare(tf32 a, tf32 b) {
    // 두 수에서 부호, 지수, 가수를 추출
    unsigned int sign_a = (a >> 18) & 1;
    unsigned int exp_a = (a >> TF32_EXP_SHIFT) & 0xFF;
    unsigned int frac_a = a & TF32_FRAC_MASK;
    unsigned int sign_b = (b >> 18) & 1;
    unsigned int exp_b = (b >> TF32_EXP_SHIFT) & 0xFF;
    unsigned int frac_b = b & TF32_FRAC_MASK;

    // NaN 처리
    if((exp_a == 0xFF && frac_a != 0) || (exp_b == 0xFF && frac_b != 0)){
        return -2;
    }

    // +0과 -0은 같다고 처리
    if((exp_a == 0 && frac_a == 0) && (exp_b == 0 && frac_b == 0)){
        return 0;
    }

    // 부호가 다른 경우
    if(sign_a != sign_b){
        if(sign_a){ // a가 음수 b가 양수일 경우 -1
            return -1;
        }else{ // 반대일 경우 1
            return 1;
        }
    }

    // 부호가 같은 경우 처리
    int comparison;
    // 비트 표현이 완전히 같은 경우 두 수는 같음
    if(a == b){
        comparison = 0;
    }else if (exp_a != exp_b){ // 지수가 다르면 지수가 큰 쪽이 절댓값이 큼
        comparison = (exp_a > exp_b) ? 1 : -1;
    } else { // 지수가 같으면 가수가 큰 쪽이 절대값이 큼
        comparison = (frac_a > frac_b) ? 1 : -1;
    }

    // 만약 두 수가 음수였다면, 절대값 비교 결과 반대로 전환
    if(sign_a){
        comparison = -comparison;
    }

    // 최종 결과 반환
    return comparison;
}
