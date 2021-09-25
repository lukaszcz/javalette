
int main()
{
   int x = 10;
   int y, z, u, v, w;
   y = 13;
   w = x + y;
   z = y + 5;
   u = z - w;
   w = w % (-u);
   w++;
   y = z * w;
   w++;
   v = 6 * y;
   v = u + v;
   z = 6 * y + 5 * u - 4 * (w + u + v / 2 + z / 7);
   x = z * 16 + u * 10;
   printInt(x);
   printInt(y);
   printInt(z);
   printInt(u);
   printInt(v);
   printInt(w);
   return 0;
}
