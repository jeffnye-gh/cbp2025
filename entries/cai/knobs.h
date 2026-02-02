#ifndef __KNOBS_H__
#define __KNOBS_H__
// CSC KNOBS
#define LOGFT 8
#define TCB 9 
#define BLS 174745
#define BLB 2
#define LOGWS 9
#define CSC_USE_THETA false

//#define BLOOM_SIZE_INDIV 40960
//#define BLOOM_SIZE 195227 * 2

// TAGE SCL KNOBS
//
// begin loop predictor parameters
#define LOGL 5
#define WIDTHNBITERLOOP 10  // we predict only loops with less than 1K iterations
#define LOOPTAG 10      //tag width in the loop predictor

#define BORNTICK  1664

#define SC          // 8.2 % if TAGE alone
#define IMLI            // 0.2 %
#define LOCALH

#ifdef LOCALH           // 2.7 %
#define LOOPPREDICTOR   //loop predictor enable
#define LOCALS          //enable the 2nd local history
#define LOCALT          //enables the 3rd local history
#endif
// end loop predictor parameters

// begin statistical corrector parameters
#define PERCWIDTH 8     //Statistical corrector  counter width 5 -> 6 : 0.6 %
//The three BIAS tables in the SC component
//We play with the TAGE  confidence here, with the number of the hitting bank
#define LOGBIAS 9
//In all th GEHL components, the two tables with the shortest history lengths have only half of the entries.
// IMLI-SIC -> Micro 2015  paper: a big disappointment on  CBP2016 traces
#ifdef IMLI
#define LOGINB 8        // 128-entry
#define INB 1
#define LOGIMNB 9       // 2* 256 -entry
#define IMNB 2
#endif
//global branch GEHL
#define LOGGNB 10       // 1 1K + 2 * 512-entry tables
#define GNB 3
//variation on global branch history
#define PNB 3
#define LOGPNB 9        // 1 1K + 2 * 512-entry tables
//first local history
#define LOGLNB 11      // 1 1K + 2 * 512-entry tables
#define LNB 3
#define  LOGLOCAL 8
#define NLOCAL (1<<LOGLOCAL)
// second local history
#define LOGSNB 9        // 1 1K + 2 * 512-entry tables
#define SNB 3
#define LOGSECLOCAL 4
#define NSECLOCAL (1<<LOGSECLOCAL)  //Number of second local histories
//third local history
#define LOGTNB 10       // 2 * 512-entry tables
#define TNB 2
#define NTLOCAL 16
// playing with putting more weights (x2)  on some of the SC components
// playing on using different update thresholds on SC
//update threshold for the statistical corrector
#define VARTHRES
#define WIDTHRES 12
#define WIDTHRESP 8
#ifdef VARTHRES
#define LOGSIZEUP 6     //not worth increasing
#else
#define LOGSIZEUP 0
#endif

#define EWIDTH 6

#define CONFWIDTH 7     //for the counters in the choser
#define HISTBUFFERLENGTH 4096   // we use a 4K entries history buffer to store the branch history (this allows us to explore using history length up to 4K)
// end statistical corrector parameters

// begin tage parameters
#define  POWER
//use geometric history length

#define NHIST 42        // twice the number of different histories

#define NBANKLOW 5 // DEFAULT is 10    // number of banks in the shared bank-interleaved for the low history lengths
#define NBANKHIGH 18 // DEFAULT is 20       // number of banks in the shared bank-interleaved for the  history lengths
#define BORN 7 // DEFAULT IS 13        // below BORN in the table for low history lengths, >= BORN in the table for high history lengths,
// we use 2-way associativity for the medium history lengths
#define BORNINFASSOC 43 // DEFAULT IS 9     //2 -way assoc for those banks 0.4 %
#define BORNSUPASSOC 42 // DEFAULT IS 23

/*in practice 2 bits or 3 bits par branch: around 1200 cond. branchs*/

#define MINHIST 6       //not optimized so far
#define MAXHIST 3000


//#define LOGG 12 // DEFAULT IS 10        /* logsize of the  banks in the  tagged TAGE tables */
#define LOGG_LOW 10
#define LOGG_HIGH 12
#define TBITS 8 // DEFAULT IS 8        //minimum width of the tags  (low history lengths), +4 for high history lengths

#define NNN 1           // number of extra entries allocated on a TAGE misprediction (1+NNN)
#define HYSTSHIFT 2     // bimodal hysteresis shared by 4 entries
#define LOGB 15 // DEFAULT IS 13         // log of number of entries in bimodal predictor


#define PHISTWIDTH 27       // width of the path history used in TAGE
#define UWIDTH 1        // u counter width on TAGE (2 bits not worth the effort for a 512 Kbits predictor 0.2 %)
#define CWIDTH 3        // predictor counter width on the TAGE tagged tables

//the counter(s) to chose between longest match and alternate prediction on TAGE when weak counters
#define LOGSIZEUSEALT 4
#define ALTWIDTH 5



//#define PERPC_STATS
//#define BRANCH_LATENCY
//#define PRINT_WPS 
//#define PRINT_ALL_LATENCIES
//#define ENTRY_STATS
//#define PRINT_AGG_ENTRY_STATS
//#define PRINT_PER_ENTRY_STATS
//#define RAS_STUDY
//#define GHIST_STUDY
//#define BANK_USAGE
//#define PRINT_PRED_CASES
//#define BRANCH_WP_CSV

// HIGH LATENCY CLASSIFICATION METHODS
//#define HIGH_LATENCY_TABLE TODO: IMPLEMENT THIS!!! MAKE SURE IT'S NOT JUST MISP AS WELL
//#define MEM_AND_UP_ALL // all branches > 215
//#define MEM_AND_UP_ON_MISP // all misp branches > 215 
//#define L3_AND_UP_ALL
//#define L3_AND_UP // just flag all branches with latency 66 and up as high latency
//#define LOW_LATENCY_TABLE
//#define HIGH_LATENCY_HEAP
//#define LAST1KPERC_LATENCY
#endif
