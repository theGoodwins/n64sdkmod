/*******************************************************************
 *
 *  main.c
 *
 *******************************************************************/

#include  <nusys.h>

#include  "mainmenu.h"
#include  "rmbmenu.h"

static void   vsyncCallback(s32 pendingTaskCount);
static void   prenmiCallback(void);
void  MainMenu(void);

/* Main functions */
void  mainproc(void * dummy)
{
  /* Initialize graphics */
  nuGfxInit();
  
  /*
   *  Initialize controller
   *    Internally 
   *      nuSiMgrInit           Initialize and launch SI Manager
   *      nuContMgrInit         Initialize and register Controller Manager
   *      nuContPakMgrInit      Initialize and register Controller Pak Manager
   *      nuContRmbMgrInit      Initialize and register Rumble Pak Manager
   *    are called 
   */
  nuContInit();
  
  /* RumblePak related initialization (defined in rmbmenu.c) */
  RmbInit();
  
  /* Register vertical trace callback */
  nuGfxFuncSet((NUGfxFunc)vsyncCallback);
  
  /* Register PRE NMI event callback function */
  nuPreNMIFuncSet((NUScPreNMIFunc)prenmiCallback);
  
  /* Video output ON */
  nuGfxDisplayOn();
  
  while(1);
}

void  vsyncCallback(s32 pendingTaskCount)
{
  /* If there are no tasks for which processing is not completed, register the 
next graphics task */
  if(pendingTaskCount < 1)
  {
    /* Drawing to the debugging console that accompanies menu processing 
is performed inside the following function */
    MainMenu();
    /* Consequently, the instruction is given here to display the debugging console */
    nuDebConDisp(NU_SC_SWAPBUFFER);
  }
}

void  prenmiCallback(void)
{
  /* Forcibly stop RumblePak */
  nuContRmbForceStop();
}
