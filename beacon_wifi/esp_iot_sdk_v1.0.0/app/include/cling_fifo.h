#ifndef __CLING_FIFO_H__
#define __CLING_FIFO_H__

/*
*单一FIFO_IN线程和单一FIFO_OUT线程，可并发操作(即无需定义CLING_FIFO_LOCK和CLING_FIFO_UNLOCK)
*牺牲一个type的FIFO单元提高队列维护的简洁性
*/
#define  CLING_FIFO_FULL_FLAG   1
#define  CLING_FIFO_NOT_FULL    0
#define  CLING_FIFO_EMPTY_FLAG  2

#define  FIFO_SEMAPHORE	bool
#define  FIFO_LOCKED    1
#define  FIFO_UNLOCKED  0


/* 重新定义以下宏，请在include CLING_FIFO.h前定义 */
#ifndef CLING_FIFO_FULL
#define CLING_FIFO_FULL(fifo, type)  os_free(fifo##_in->pbuffer);\
 									  fifo##_flag = CLING_FIFO_FULL_FLAG;\
 									  fifo##_num --;

#endif

#ifndef CLING_FIFO_EMPTY
#define CLING_FIFO_EMPTY(fifo)	fifo##_flag = CLING_FIFO_EMPTY_FLAG
#endif

#ifndef CLING_FIFO_LOCK
#define CLING_FIFO_LOCK(fifo)  if (fifo##_lock == FIFO_LOCKED){\
									break;\
 								}\
 								fifo##_lock = FIFO_LOCKED;
#endif

#ifndef CLING_FIFO_UNLOCK
#define CLING_FIFO_UNLOCK(fifo) fifo##_lock = FIFO_UNLOCKED;
#endif



/*create fifo*/
#define CREAT_CLING_FIFO(fifo,type,size)                          \
 				 FIFO_SEMAPHORE	fifo##_lock = FIFO_UNLOCKED;			\
                 type  fifo[size];                              \
                 type *fifo##_in = fifo;                        \
                 type *fifo##_out = fifo;                 \
                 uint16 fifo##_num = 0;\
				 uint8 fifo##_flag = CLING_FIFO_EMPTY_FLAG
/* declare FIFO */
#define DECLARE_CLING_FIFO(fifo,type,size)                  \
                 extern type  fifo[size];                 \
                 extern type  *fifo##_in;                 \
                 extern type  *fifo##_out

/*initiate FIFO */
#define INIT_CLING_FIFO(fifo,size)         \
         do {                            \
             fifo##_in = fifo;           \
             fifo##_out = fifo;          \
         } while(0)

/*write FIFO */
#define CLING_FIFO_IN(fifo, data, type)                                       \
         do {                                                            \
             CLING_FIFO_LOCK(fifo);                                         \
             *(fifo##_in++) = data;                                      \
              fifo##_num ++;					\
		 	 if ( fifo##_num + 1 >= (sizeof(fifo) / sizeof(type)) ) {\
				 fifo##_flag = CLING_FIFO_FULL_FLAG;\
			 }\
             if (fifo##_in >= fifo + sizeof(fifo) / sizeof(type))        \
                 fifo##_in = fifo;                                       \
             if (fifo##_in == fifo##_out) { 							\
                 if (fifo##_in-- == fifo)                                \
                     fifo##_in = fifo + sizeof(fifo) / sizeof(type) - 1; \
  						CLING_FIFO_FULL(fifo, type);                   	\
             }else{\
					fifo##_flag = CLING_FIFO_NOT_FULL;\
				}                                                           \
             CLING_FIFO_UNLOCK(fifo);                                       \
         } while(0)


/* read FIFO */
#define CLING_FIFO_OUT(fifo, data, type)                                   \
         do {                                                          \
             CLING_FIFO_LOCK(fifo);                                       \
             if (fifo##_in == fifo##_out) {                            \
				 CLING_FIFO_EMPTY(fifo);  \
			     fifo##_num = 0;\
             }                                                         \
             else {                                                    \
			 	 fifo##_flag = CLING_FIFO_NOT_FULL;\
                 *data = *(fifo##_out++);                              \
                 if (fifo##_num > 0){\
					fifo##_num--;		\
				 }\
                 if (fifo##_out >= fifo + sizeof(fifo) / sizeof(type)) \
                     fifo##_out = fifo;                                \
             }                                                         \
             CLING_FIFO_UNLOCK(fifo);                                     \
         } while(0)

#define  IS_CLING_FIFO_FULL(fifo) (fifo##_flag == CLING_FIFO_FULL_FLAG)
#define  IS_CLING_FIFO_EMPTY(fifo) (fifo##_flag == CLING_FIFO_EMPTY_FLAG)

#endif
