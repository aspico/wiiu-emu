#include "padscore.h"

PadScore::PadScore()
{
}

void
PadScore::initialise()
{
}

void
PadScore::RegisterFunctions()
{
   registerKPADFunctions();
   registerVPADFunctions();
   registerWPADFunctions();
}
