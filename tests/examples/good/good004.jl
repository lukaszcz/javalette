
int main()
{
   int a[125];
   int i;
   for (i = 0; i < 125; i++)
   {
      a[i] = i*i;
   }
   for (i = 0; i < 125; i++)
   {
      a[i] = a[a[i] % 125] * 7;
   }
   printInt(a[0]);
   printInt(a[13]);
   printInt(a[124]);
   return 0;
}
