//  Copyright © 2015 David Caldwell <david@porkrind.org>
//
// Permission to use, copy, modify, distribute, and sell this software and its
// documentation for any purpose is hereby granted without fee, provided that
// the above copyright notice appear in all copies and that both that
// copyright notice and this permission notice appear in supporting
// documentation.  No representations are made about the suitability of this
// software for any purpose.  It is provided "as is" without express or
// implied warranty.

#ifndef __RANDOM_H___
#define __RANDOM_H___

#define frand(f) fabs((((double) rand()) * ((double) (f))) / ((double)RAND_MAX))
#define random() rand()

#endif /* __RANDOM_H___ */
