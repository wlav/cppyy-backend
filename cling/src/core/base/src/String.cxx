// @(#)root/base:$Id$
// Author: Philippe Canal 03/09/2003

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun, Fons Rademakers and al.           *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// std::string helper utilities                                         //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include <ROOT/RConfig.hxx>
#include <string>
#include "TBuffer.h"


namespace CppyyLegacy {

void std_string_streamer(TBuffer &b, void *objadd)
{
   // Streamer function for std::string object.
   if (b.IsReading()) {
      b.ReadStdString((std::string*)objadd);
   } else {
      b.WriteStdString((std::string*)objadd);
   }
}

} // namespace CppyyLegacy

// Declare the streamer to the string TClass object
RootStreamer(std::string,std_string_streamer);

// Set a version number of the string TClass object
RootClassVersion(std::string,2);
