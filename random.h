//  Copyright © 2015 David Caldwell <david@porkrind.org>
#ifndef __RANDOM_H___
#define __RANDOM_H___

#define frand(f) fabs((((double) rand()) * ((double) (f))) / ((double)RAND_MAX))
#define random() rand()

#endif /* __RANDOM_H___ */
