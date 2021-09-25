
int main()
{
   f(10, 7);
   g(2, 3);
   h(3, 4, 5);
   return 0;
}

void f(int x, int y)
{
   int v, w;
   w = x + y;
   v = x + y;
   int z = v + 2 * w;
   printInt(z);
}

void g(int u, int v)
{
   int z = (v * u) + (u * v) - (3 * ((u * v) + (v * u)));
   printInt(z);
}

void h(int x, int y, int z)
{
   printInt(x * y + x * z);
}
