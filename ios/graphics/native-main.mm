#import <Foundation/Foundation.h>
extern "C" NSDictionary *osl_mobile_gpu_report();
int main(int argc,char **argv) {
 @autoreleasepool {
  NSDictionary *result=osl_mobile_gpu_report();
  NSData *data=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:NULL];
  if(argc==2) [data writeToFile:@(argv[1]) options:NSDataWritingAtomic error:NULL];
  puts([[NSString alloc]initWithData:data encoding:NSUTF8StringEncoding].UTF8String);
  return [result[@"passed"] boolValue]?0:1;
 }
}
