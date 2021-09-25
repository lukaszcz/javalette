
int
main()
{
  printDouble(power(3.14, 7));
  printDouble(power(2.46, 4));
  return 0;
}

/* complexity: O(log(b)) */
double power(double a, int b)
{
  double ta;
  double tb;

  if (b != 0)
  {
      ta = a;
      tb = 1.0;
      while (b > 1)
      {
         if ((b % 2) != 0)
         {
            tb = tb * ta;
         }
         ta = ta * ta;
         b = b / 2;
      }
      return ta * tb;
   }
   else
   {
      return 1.0;
   }
}

