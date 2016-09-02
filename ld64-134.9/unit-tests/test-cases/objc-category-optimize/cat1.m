#include <Foundation/Foundation.h>

@interface Foo : NSObject
-(void) method1;
@end




@interface Foo(mycat)
-(void) instance_method_mycat1;
-(void) instance_method_mycat2;
+(void) class_method_mycat;
#if PROPERTIES
	@property(readonly) int property1;
	@property(readonly) int property2;
#endif
@end

@implementation Foo(mycat)
-(void) instance_method_mycat1 {}
-(void) instance_method_mycat2 {}
+(void) class_method_mycat {}
#if PROPERTIES
	-(int) property1 { return 0; }
	-(int) property2 { return 0; }
#endif
@end

