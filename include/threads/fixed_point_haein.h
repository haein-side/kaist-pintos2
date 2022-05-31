#define F (1 << 14)
#define INT_MAX ((1 << 31) - 1)
#define INT_MIN (-(1 << 31))

int int_to_fp (int n) {
  return n * F;
}

int fp_to_int (int x) {
  return x / F;
}

int fp_to_int_round (int x) {
  if (x >= 0) return (x + F / 2) / F;
  else return (x - F / 2) / F;
}

int add_fp (int x, int y) {
  return x + y;
}

int sub_fp (int x, int y) {
  return x - y;
}

int add_mixed (int x, int n) {
  return x + n * F;
}

int sub_mixed (int x, int n) {
  return x - n * F;
}

int mult_fp (int x, int y) {
  return ((int64_t) x) * y / F; 
}
// 두 개의 fp 를 곱하면 32bit * 32bit 로 32bit 를 초과하는 값이 발생
// 64bit 로 일시적으로 바꿔준 후 곱하기 연산을 하여 오버플로우를 방지
// 64bit 값을 F(0 00000000000000001 00000000000000)로 나눠서 다시 32bit 값으로 만들어 줌

int mult_mixed (int x, int n) {
  return x * n;
}

int div_fp (int x, int y) {
  return ((int64_t) x) * F / y;
}

int div_mixed (int x, int n) {
  return x / n;
}