#include <math.h>

int max(int a, int b)
{
	return a>b ? a : b;
}

int min(int a, int b)
{
	return a<b ? a : b;
}

int abs(int n)
{
	return n>=0 ? n : -n;
}

int pow(int x, int y)
{
	int i, ans = x;
	for(i = 0; i < y; i++)
	{
		ans *= x;
	}
	return ans;
}