#include "v8_c_bridge.h"

#include "libplatform/libplatform.h"
#include "v8.h"
#include "v8-profiler.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <stdio.h>
#include <iostream>
#include <thread>

#define ISOLATE_SCOPE(iso) \
  v8::Isolate* isolate = (iso);                                                               \
  v8::Locker locker(isolate);                            /* Lock to current thread.        */ \
  fprintf(stderr, "ISOLATE LOCKED!\n"); \
  v8::Isolate::Scope isolate_scope(isolate);             /* Assign isolate to this thread. */


#define VALUE_SCOPE(ctxptr) \
  ISOLATE_SCOPE(static_cast<Context*>(ctxptr)->isolate)                                       \
  v8::HandleScope handle_scope(isolate);                 /* Create a scope for handles.    */ \
  v8::Local<v8::Context> ctx(static_cast<Context*>(ctxptr)->ptr.Get(isolate));                \
  v8::Context::Scope context_scope(ctx);                 /* Scope to this context.         */

// extern "C" ValueErrorPair go_callback_handler(
//     String id, CallerInfo info, int argc, ValueKindsPair* argv);

class ArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
 public:
  virtual void* Allocate(size_t length);
  virtual void* AllocateUninitialized(size_t length);
  virtual void Free(void* data, size_t);
};
void* ArrayBufferAllocator::Allocate(size_t length) {
  void* data = AllocateUninitialized(length);
  return data == nullptr ? data : memset(data, 0, length);
}
void* ArrayBufferAllocator::AllocateUninitialized(size_t length) { return malloc(length); }
void ArrayBufferAllocator::Free(void* data, size_t) { free(data); }

// We only need one, it's stateless.
ArrayBufferAllocator allocator;

typedef struct {
  v8::Persistent<v8::Context> ptr;
  v8::Isolate* isolate;
} Context;

typedef v8::Persistent<v8::Value> Value;

String str_to_cr_str(const v8::String::Utf8Value& src) {
  char* data = static_cast<char*>(malloc(src.length()));
  memcpy(data, *src, src.length());
  return (String){data, src.length()};
}
String str_to_cr_str(const v8::Local<v8::Value>& val) {
  return str_to_cr_str(v8::String::Utf8Value(val));
}
String str_to_cr_str(const char* msg) {
  const char* data = strdup(msg);
  return (String){data, int(strlen(msg))};
}
String str_to_cr_str(const std::string& src) {
  char* data = static_cast<char*>(malloc(src.length()));
  memcpy(data, src.data(), src.length());
  return (String){data, int(src.length())};
}

ValueKinds v8_Value_KindsFromLocal(v8::Local<v8::Value> value) {
  std::vector<uint8_t> kinds;

  if (value->IsUndefined())         kinds.push_back(ValueKind::kUndefined        );
  if (value->IsNull())              kinds.push_back(ValueKind::kNull             );
  if (value->IsName())              kinds.push_back(ValueKind::kName             );
  if (value->IsString())            kinds.push_back(ValueKind::kString           );
  if (value->IsSymbol())            kinds.push_back(ValueKind::kSymbol           );
  if (value->IsObject())            kinds.push_back(ValueKind::kObject           );
  if (value->IsArray())             kinds.push_back(ValueKind::kArray            );
  if (value->IsBoolean())           kinds.push_back(ValueKind::kBoolean          );
  if (value->IsTrue())              kinds.push_back(ValueKind::kTrue             );
  if (value->IsFalse())             kinds.push_back(ValueKind::kFalse            );
  if (value->IsNumber())            kinds.push_back(ValueKind::kNumber           );
  if (value->IsExternal())          kinds.push_back(ValueKind::kExternal         );
  if (value->IsInt32())             kinds.push_back(ValueKind::kInt32            );
  if (value->IsUint32())            kinds.push_back(ValueKind::kUint32           );
  if (value->IsDate())              kinds.push_back(ValueKind::kDate             );
  if (value->IsArgumentsObject())   kinds.push_back(ValueKind::kArgumentsObject  );
  if (value->IsBooleanObject())     kinds.push_back(ValueKind::kBooleanObject    );
  if (value->IsNumberObject())      kinds.push_back(ValueKind::kNumberObject     );
  if (value->IsStringObject())      kinds.push_back(ValueKind::kStringObject     );
  if (value->IsSymbolObject())      kinds.push_back(ValueKind::kSymbolObject     );
  if (value->IsNativeError())       kinds.push_back(ValueKind::kNativeError      );
  if (value->IsRegExp())            kinds.push_back(ValueKind::kRegExp           );
  if (value->IsFunction())          kinds.push_back(ValueKind::kFunction         );
  if (value->IsAsyncFunction())     kinds.push_back(ValueKind::kAsyncFunction    );
  if (value->IsGeneratorFunction()) kinds.push_back(ValueKind::kGeneratorFunction);
  if (value->IsGeneratorObject())   kinds.push_back(ValueKind::kGeneratorObject  );
  if (value->IsPromise())           kinds.push_back(ValueKind::kPromise          );
  if (value->IsMap())               kinds.push_back(ValueKind::kMap              );
  if (value->IsSet())               kinds.push_back(ValueKind::kSet              );
  if (value->IsMapIterator())       kinds.push_back(ValueKind::kMapIterator      );
  if (value->IsSetIterator())       kinds.push_back(ValueKind::kSetIterator      );
  if (value->IsWeakMap())           kinds.push_back(ValueKind::kWeakMap          );
  if (value->IsWeakSet())           kinds.push_back(ValueKind::kWeakSet          );
  if (value->IsArrayBuffer())       kinds.push_back(ValueKind::kArrayBuffer      );
  if (value->IsArrayBufferView())   kinds.push_back(ValueKind::kArrayBufferView  );
  if (value->IsTypedArray())        kinds.push_back(ValueKind::kTypedArray       );
  if (value->IsUint8Array())        kinds.push_back(ValueKind::kUint8Array       );
  if (value->IsUint8ClampedArray()) kinds.push_back(ValueKind::kUint8ClampedArray);
  if (value->IsInt8Array())         kinds.push_back(ValueKind::kInt8Array        );
  if (value->IsUint16Array())       kinds.push_back(ValueKind::kUint16Array      );
  if (value->IsInt16Array())        kinds.push_back(ValueKind::kInt16Array       );
  if (value->IsUint32Array())       kinds.push_back(ValueKind::kUint32Array      );
  if (value->IsInt32Array())        kinds.push_back(ValueKind::kInt32Array       );
  if (value->IsFloat32Array())      kinds.push_back(ValueKind::kFloat32Array     );
  if (value->IsFloat64Array())      kinds.push_back(ValueKind::kFloat64Array     );
  if (value->IsDataView())          kinds.push_back(ValueKind::kDataView         );
  if (value->IsSharedArrayBuffer()) kinds.push_back(ValueKind::kSharedArrayBuffer);
  if (value->IsProxy())             kinds.push_back(ValueKind::kProxy            );
  if (value->IsWebAssemblyCompiledModule()) kinds.push_back(ValueKind::kWebAssemblyCompiledModule);

  uint8_t* data = static_cast<uint8_t*>(calloc(kinds.size(), sizeof(uint8_t)));
  memcpy(data, kinds.data(), kinds.size() * sizeof(uint8_t));
  return (ValueKinds){data, kinds.size()};
}

std::string str(v8::Local<v8::Value> value) {
  v8::String::Utf8Value s(value);
  if (s.length() == 0) {
    return "";
  }
  return *s;
}

std::string report_exception(v8::Isolate* isolate, v8::Local<v8::Context> ctx, v8::TryCatch& try_catch) {
  std::stringstream ss;
  ss << "Uncaught exception: ";

  std::string exceptionStr = str(try_catch.Exception());
  ss << exceptionStr; // TODO(aroman) JSON-ify objects?

  fprintf(stderr, "message is empty? %d\n", try_catch.Message().IsEmpty());
  if (!try_catch.Message().IsEmpty()) {
    if (!try_catch.Message()->GetScriptResourceName()->IsUndefined()) {
      fprintf(stderr, "res name not undefined");
      ss << std::endl
         << "at " << str(try_catch.Message()->GetScriptResourceName());

      v8::Maybe<int> line_no = try_catch.Message()->GetLineNumber(ctx);
      v8::Maybe<int> start = try_catch.Message()->GetStartColumn(ctx);
      v8::Maybe<int> end = try_catch.Message()->GetEndColumn(ctx);
      v8::MaybeLocal<v8::String> sourceLine = try_catch.Message()->GetSourceLine(ctx);

      if (line_no.IsJust()) {
        ss << ":" << line_no.ToChecked();
      }
      if (start.IsJust()) {
        ss << ":" << start.ToChecked();
      }
      if (!sourceLine.IsEmpty()) {
        ss << std::endl
           << "  " << str(sourceLine.ToLocalChecked());
      }
      if (start.IsJust() && end.IsJust()) {
        ss << std::endl
           << "  ";
        for (int i = 0; i < start.ToChecked(); i++) {
          ss << " ";
        }
        for (int i = start.ToChecked(); i < end.ToChecked(); i++) {
          ss << "^";
        }
      }
    }
  }

  if (!try_catch.StackTrace().IsEmpty()) {
    ss << std::endl << "Stack trace: " << str(try_catch.StackTrace());
  }

  return ss.str();
}


extern "C" {

Version v8_Version() {
  return (Version){V8_MAJOR_VERSION, V8_MINOR_VERSION, V8_BUILD_NUMBER, V8_PATCH_LEVEL};
}

void v8_init() {
  v8::Platform *platform = v8::platform::CreateDefaultPlatform();
  v8::V8::InitializePlatform(platform);
  v8::V8::Initialize();
  return;
}

StartupData v8_CreateSnapshotDataBlob(const char* js) {
  v8::StartupData data = v8::V8::CreateSnapshotDataBlob(js);
  return StartupData{data.data, data.raw_size};
}

IsolatePtr v8_Isolate_New(StartupData startup_data) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = &allocator;
  if (startup_data.len > 0 && startup_data.ptr != nullptr) {
    v8::StartupData* data = new v8::StartupData;
    data->data = startup_data.ptr;
    data->raw_size = startup_data.len;
    create_params.snapshot_blob = data;
  }
  return static_cast<IsolatePtr>(v8::Isolate::New(create_params));
}
ContextPtr v8_Isolate_NewContext(IsolatePtr isolate_ptr) {
  ISOLATE_SCOPE(static_cast<v8::Isolate*>(isolate_ptr));
  v8::HandleScope handle_scope(isolate);

  isolate->SetCaptureStackTraceForUncaughtExceptions(true);

  v8::Local<v8::ObjectTemplate> globals = v8::ObjectTemplate::New(isolate);

  Context* ctx = new Context;
  ctx->ptr.Reset(isolate, v8::Context::New(isolate, nullptr, globals));
  ctx->isolate = isolate;
  return static_cast<ContextPtr>(ctx);
}
void v8_Isolate_Terminate(IsolatePtr isolate_ptr) {
  v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_ptr);
  isolate->TerminateExecution();
}
void v8_Isolate_Release(IsolatePtr isolate_ptr) {
  if (isolate_ptr == nullptr) {
    return;
  }
  v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_ptr);
  isolate->Dispose();
}

ValueErrorPair v8_Context_Run(ContextPtr ctxptr, const char* code, const char* filename) {
  Context* ctx = static_cast<Context*>(ctxptr);
  v8::Isolate* isolate = ctx->isolate;
  v8::Locker locker(isolate);
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(ctx->ptr.Get(isolate));
  v8::TryCatch try_catch(isolate);
  try_catch.SetVerbose(false);

  filename = filename ? filename : "(no file)";

  ValueErrorPair res = { nullptr, nullptr };

  v8::Local<v8::Script> script = v8::Script::Compile(
      v8::String::NewFromUtf8(isolate, code),
      v8::String::NewFromUtf8(isolate, filename));

  if (script.IsEmpty()) {
    res.error_msg = str_to_cr_str(report_exception(isolate, ctx->ptr.Get(isolate), try_catch));
    return res;
  }

  v8::Local<v8::Value> result = script->Run();

  if (result.IsEmpty()) {
    res.error_msg = str_to_cr_str(report_exception(isolate, ctx->ptr.Get(isolate), try_catch));
  } else {
    res.Value = static_cast<PersistentValuePtr>(new Value(isolate, result));
    // res.Kinds = v8_Value_KindsFromLocal(result);
  }

	return res;
}

void crystal_callback(const v8::FunctionCallbackInfo<v8::Value>& args);

PersistentValuePtr v8_FunctionTemplate_New(ContextPtr ctxptr, const char* name, const char* id) {
  VALUE_SCOPE(ctxptr);
  std::cout << "fn tpl thread " << std::this_thread::get_id() << "\n";
  v8::Local<v8::FunctionTemplate> cb = v8::FunctionTemplate::New(
    isolate,
    crystal_callback,
    v8::String::NewFromUtf8(isolate, id)
  );
  cb->SetClassName(v8::String::NewFromUtf8(isolate, name));
  return new Value(isolate, cb->GetFunction());
}

PersistentValuePtr __crystal_v8_callback_handler(String id, int argc, PersistentValuePtr* argv);

void crystal_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* iso = args.GetIsolate();
  v8::HandleScope scope(iso);

  std::string id = str(args.Data());

  std::string src_file, src_func;
  int line_number = 0, column = 0;
  v8::Local<v8::StackTrace> trace(v8::StackTrace::CurrentStackTrace(iso, 1));
  if (trace->GetFrameCount() == 1) {
    v8::Local<v8::StackFrame> frame(trace->GetFrame(0));
    src_file = str(frame->GetScriptName());
    src_func = str(frame->GetFunctionName());
    line_number = frame->GetLineNumber();
    column = frame->GetColumn();
  }

  int argc = args.Length();
  PersistentValuePtr argv[argc];
  for (int i = 0; i < argc; i++) {
    argv[i] = new Value(iso, args[i]);
    fprintf(stderr, "pointer of arg %i is %p\n", i, argv[i]);
  }
  fprintf(stderr, "sizeof argv %lu\n", sizeof(argv));

  PersistentValuePtr result = __crystal_v8_callback_handler((String){id.data(), int(id.length())}, argc, argv);

  fprintf(stderr, "done with crystal cb\n");

  if (result == nullptr) {
    args.GetReturnValue().Set(v8::Undefined(iso));
  } else {
    args.GetReturnValue().Set(*static_cast<Value*>(result));
  }

  // if (result.error_msg.ptr != nullptr) {
  //   v8::Local<v8::Value> err = v8::Exception::Error(
  //     v8::String::NewFromUtf8(isolate, result.error_msg.ptr, v8::NewStringType::kNormal, result.error_msg.len).ToLocalChecked());
  //   isolate->ThrowException(err);
  // } else if (result.Value == NULL) {
  //   args.GetReturnValue().Set(v8::Undefined(isolate));
  // } else {
  //   args.GetReturnValue().Set(*static_cast<Value*>(result.Value));
  // }
}

// void go_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
//   v8::Isolate* iso = args.GetIsolate();
//   v8::HandleScope scope(iso);

//   std::string id = str(args.Data());

//   std::string src_file, src_func;
//   int line_number = 0, column = 0;
//   v8::Local<v8::StackTrace> trace(v8::StackTrace::CurrentStackTrace(iso, 1));
//   if (trace->GetFrameCount() == 1) {
//     v8::Local<v8::StackFrame> frame(trace->GetFrame(0));
//     src_file = str(frame->GetScriptName());
//     src_func = str(frame->GetFunctionName());
//     line_number = frame->GetLineNumber();
//     column = frame->GetColumn();
//   }

//   int argc = args.Length();
//   ValueKindsPair argv[argc];
//   for (int i = 0; i < argc; i++) {
//     argv[i] = (ValueKindsPair){new Value(iso, args[i]), v8_Value_KindsFromLocal(args[i])};
//   }

//   ValueErrorPair result =
//       go_callback_handler(
//         (String){id.data(), int(id.length())},
//         (CallerInfo){
//           (String){src_func.data(), int(src_func.length())},
//           (String){src_file.data(), int(src_file.length())},
//           line_number,
//           column
//         },
//         argc, argv);

//   if (result.error_msg.ptr != nullptr) {
//     v8::Local<v8::Value> err = v8::Exception::Error(
//       v8::String::NewFromUtf8(iso, result.error_msg.ptr, v8::NewStringType::kNormal, result.error_msg.len).ToLocalChecked());
//     iso->ThrowException(err);
//   } else if (result.Value == NULL) {
//     args.GetReturnValue().Set(v8::Undefined(iso));
//   } else {
//     args.GetReturnValue().Set(*static_cast<Value*>(result.Value));
//   }
// }

PersistentValuePtr v8_Context_Global(ContextPtr ctxptr) {
  VALUE_SCOPE(ctxptr);
  return new Value(isolate, ctx->Global());
}

void v8_Context_Release(ContextPtr ctxptr) {
  fprintf(stderr, "RELEASING A VALUE NOW\n");
  if (ctxptr == nullptr) {
    return;
  }
  Context* ctx = static_cast<Context*>(ctxptr);
  ISOLATE_SCOPE(ctx->isolate);
  ctx->ptr.Reset();
}

PersistentValuePtr v8_Context_Create(ContextPtr ctxptr, ImmediateValue val) {
  VALUE_SCOPE(ctxptr);

  switch (val.Type) {
    case tSTRING:
      return new Value(isolate, v8::String::NewFromUtf8(
        isolate, val.Str.ptr, v8::NewStringType::kNormal, val.Str.len).ToLocalChecked());
    case tNUMBER:      return new Value(isolate, v8::Number::New(isolate, val.Num));                    break;
    case tBOOL:        return new Value(isolate, v8::Boolean::New(isolate, val.BoolVal == 1));          break;
    case tOBJECT:      return new Value(isolate, v8::Object::New(isolate));                             break;
    case tARRAY:       return new Value(isolate, v8::Array::New(isolate, val.Len));                     break;
    case tARRAYBUFFER: {
        v8::Local<v8::ArrayBuffer> buf = v8::ArrayBuffer::New(isolate, val.Len);
        memcpy(buf->GetContents().Data(), val.Bytes, val.Len);
        return new Value(isolate, buf);
    } break;
    case tUNDEFINED:   return new Value(isolate, v8::Undefined(isolate));                               break;
  }
  return nullptr;
}

ValueErrorPair v8_Value_Get(ContextPtr ctxptr, PersistentValuePtr valueptr, const char* field) {
  VALUE_SCOPE(ctxptr);

  Value* value = static_cast<Value*>(valueptr);
  v8::Local<v8::Value> maybeObject = value->Get(isolate);
  if (!maybeObject->IsObject()) {
    return (ValueErrorPair){nullptr, str_to_cr_str("Not an object")};
  }

  // We can safely call `ToLocalChecked`, because
  // we've just created the local object above.
  v8::Local<v8::Object> object = maybeObject->ToObject(ctx).ToLocalChecked();

  v8::Local<v8::Value> localValue = object->Get(ctx, v8::String::NewFromUtf8(isolate, field)).ToLocalChecked();

  return (ValueErrorPair){new Value(isolate, localValue), nullptr};
}

ValueTuple v8_Value_GetIdx(ContextPtr ctxptr, PersistentValuePtr valueptr, int idx) {
  VALUE_SCOPE(ctxptr);

  Value* value = static_cast<Value*>(valueptr);
  v8::Local<v8::Value> maybeObject = value->Get(isolate);
  if (!maybeObject->IsObject()) {
    return (ValueTuple){nullptr, nullptr, 0, str_to_cr_str("Not an object")};
  }

  v8::Local<v8::Value> obj;
  if (maybeObject->IsArrayBuffer()) {
    v8::ArrayBuffer* bufPtr = v8::ArrayBuffer::Cast(*maybeObject);
    if (idx < bufPtr->GetContents().ByteLength()) {
      obj = v8::Number::New(isolate, ((unsigned char*)bufPtr->GetContents().Data())[idx]);
    } else {
      obj = v8::Undefined(isolate);
    }
  } else {
    // We can safely call `ToLocalChecked`, because
    // we've just created the local object above.
    v8::Local<v8::Object> object = maybeObject->ToObject(ctx).ToLocalChecked();
    obj = object->Get(ctx, uint32_t(idx)).ToLocalChecked();
  }
  return (ValueTuple){new Value(isolate, obj), v8_Value_KindsFromLocal(obj), nullptr};
}

Error v8_Value_Set(ContextPtr ctxptr, PersistentValuePtr valueptr,
                   const char* field, PersistentValuePtr new_valueptr) {
  VALUE_SCOPE(ctxptr);

  Value* value = static_cast<Value*>(valueptr);
  v8::Local<v8::Value> maybeObject = value->Get(isolate);
  if (!maybeObject->IsObject()) {
    return str_to_cr_str("Not an object");
  }

  // We can safely call `ToLocalChecked`, because
  // we've just created the local object above.
  v8::Local<v8::Object> object =
      maybeObject->ToObject(ctx).ToLocalChecked();


  Value* new_value = static_cast<Value*>(new_valueptr);
  v8::Local<v8::Value> new_value_local = new_value->Get(isolate);
  v8::Maybe<bool> res =
    object->Set(ctx, v8::String::NewFromUtf8(isolate, field), new_value_local);

  if (res.IsNothing()) {
    return str_to_cr_str("Something went wrong -- set returned nothing.");
  } else if (!res.FromJust()) {
    return str_to_cr_str("Something went wrong -- set failed.");
  }

  return (Error){nullptr, 0};
}

Error v8_Value_SetIdx(ContextPtr ctxptr, PersistentValuePtr valueptr,
                      int idx, PersistentValuePtr new_valueptr) {
  VALUE_SCOPE(ctxptr);

  Value* value = static_cast<Value*>(valueptr);
  v8::Local<v8::Value> maybeObject = value->Get(isolate);
  if (!maybeObject->IsObject()) {
    return str_to_cr_str("Not an object");
  }

  Value* new_value = static_cast<Value*>(new_valueptr);
  v8::Local<v8::Value> new_value_local = new_value->Get(isolate);
  if (maybeObject->IsArrayBuffer()) {
    v8::ArrayBuffer* bufPtr = v8::ArrayBuffer::Cast(*maybeObject);
    if (!new_value_local->IsNumber()) {
      return str_to_cr_str("Cannot assign non-number into array buffer");
    } else if (idx >= bufPtr->GetContents().ByteLength()) {
      return str_to_cr_str("Cannot assign to an index beyond the size of an array buffer");
    } else {
      ((unsigned char*)bufPtr->GetContents().Data())[idx] = new_value_local->ToNumber(ctx).ToLocalChecked()->Value();
    }
  } else {
    // We can safely call `ToLocalChecked`, because
    // we've just created the local object above.
    v8::Local<v8::Object> object = maybeObject->ToObject(ctx).ToLocalChecked();

    v8::Maybe<bool> res = object->Set(ctx, uint32_t(idx), new_value_local);

    if (res.IsNothing()) {
      return str_to_cr_str("Something went wrong -- set returned nothing.");
    } else if (!res.FromJust()) {
      return str_to_cr_str("Something went wrong -- set failed.");
    }
  }

  return (Error){nullptr, 0};
}

ValueErrorPair v8_Function_Call(ContextPtr ctxptr,
                             PersistentValuePtr funcptr,
                             PersistentValuePtr selfptr,
                             int argc, PersistentValuePtr* argvptr) {
  VALUE_SCOPE(ctxptr);

  std::cout << "thread " << std::this_thread::get_id() << "\n";

  fprintf(stderr, "call: got value scope\n");
  v8::TryCatch try_catch(isolate);
  try_catch.SetVerbose(false);
  fprintf(stderr, "call: got try catch\n");

  v8::Local<v8::Value> func_val = static_cast<Value*>(funcptr)->Get(isolate);
  if (!func_val->IsFunction()) {
    return (ValueErrorPair){nullptr, str_to_cr_str("Not a function")};
  }
  fprintf(stderr, "call: got func val\n");
  v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(func_val);
  fprintf(stderr, "call: got func\n");

  v8::Local<v8::Value> self;
  if (selfptr == nullptr) {
    fprintf(stderr, "call: self is null\n");
    self = v8::Undefined(isolate);
  } else {
    fprintf(stderr, "call: self is NOT null!\n");
    self = static_cast<Value*>(selfptr)->Get(isolate);
  }
  fprintf(stderr, "call: got this value\n");

  v8::Local<v8::Value>* argv = new v8::Local<v8::Value>[argc];
  for (int i = 0; i < argc; i++) {
    fprintf(stderr, "call: trying to assign something\n");
    argv[i] = static_cast<Value*>(argvptr[i])->Get(isolate);
  }
  fprintf(stderr, "call: made argv length: %d %p %p\n", argc, argvptr, argv);

  v8::MaybeLocal<v8::Value> result = func->Call(ctx, self, argc, argv);
  fprintf(stderr, "call: got maybe res\n");

  delete[] argv;

  if (result.IsEmpty()) {
    fprintf(stderr, "call: is empty :(\n");
    return (ValueErrorPair){nullptr, str_to_cr_str(report_exception(isolate, ctx, try_catch))};
  }

  v8::Local<v8::Value> value = result.ToLocalChecked();
  fprintf(stderr, "call: value to local checked\n");
  return (ValueErrorPair){
    static_cast<PersistentValuePtr>(new Value(isolate, value)),
    nullptr
  };
}

PersistentValuePtr v8_Object_New(ContextPtr ctxptr) {
  VALUE_SCOPE(ctxptr);
  v8::Local<v8::Object> obj = v8::Object::New(isolate);
  return new Value(isolate, obj);
}

ValueTuple v8_Value_New(ContextPtr ctxptr,
                            PersistentValuePtr funcptr,
                            int argc, PersistentValuePtr* argvptr) {
  VALUE_SCOPE(ctxptr);

  v8::TryCatch try_catch(isolate);
  try_catch.SetVerbose(false);

  v8::Local<v8::Value> func_val = static_cast<Value*>(funcptr)->Get(isolate);
  if (!func_val->IsFunction()) {
    return (ValueTuple){nullptr, nullptr, 0, str_to_cr_str("Not a function")};
  }
  v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(func_val);

  v8::Local<v8::Value>* argv = new v8::Local<v8::Value>[argc];
  for (int i = 0; i < argc; i++) {
    argv[i] = static_cast<Value*>(argvptr[i])->Get(isolate);
  }

  v8::MaybeLocal<v8::Object> result = func->NewInstance(ctx, argc, argv);

  delete[] argv;

  if (result.IsEmpty()) {
    return (ValueTuple){nullptr, nullptr, 0, str_to_cr_str(report_exception(isolate, ctx, try_catch))};
  }

  v8::Local<v8::Value> value = result.ToLocalChecked();
  return (ValueTuple){
    static_cast<PersistentValuePtr>(new Value(isolate, value)),
    v8_Value_KindsFromLocal(value),
    nullptr
  };
}

void v8_Value_Release(ContextPtr ctxptr, PersistentValuePtr valueptr) {
  if (valueptr == nullptr || ctxptr == nullptr)  {
    return;
  }

  ISOLATE_SCOPE(static_cast<Context*>(ctxptr)->isolate);

  Value* value = static_cast<Value*>(valueptr);
  value->Reset();
  delete value;
}

String v8_Value_String(ContextPtr ctxptr, PersistentValuePtr valueptr) {
  VALUE_SCOPE(ctxptr);

  fprintf(stderr, "got my value scope, value %p\n", valueptr);

  v8::Local<v8::Value> value = static_cast<Value*>(valueptr)->Get(isolate);
  fprintf(stderr, "got my value\n");
  return str_to_cr_str(value->ToString());
}

double v8_Value_Float64(ContextPtr ctxptr, PersistentValuePtr valueptr) {
  VALUE_SCOPE(ctxptr);
  v8::Local<v8::Value> value = static_cast<Value*>(valueptr)->Get(isolate);
  v8::Maybe<double> val = value->NumberValue(ctx);
  // This should never happen since we shouldn't be calling this unless we know
  // that it's the appropriate type via the kind checks.
  if (val.IsNothing()) {
    return 0;
  }
  return val.ToChecked();
}
int64_t v8_Value_Int64(ContextPtr ctxptr, PersistentValuePtr valueptr) {
  VALUE_SCOPE(ctxptr);
  v8::Local<v8::Value> value = static_cast<Value*>(valueptr)->Get(isolate);
  v8::Maybe<int64_t> val = value->IntegerValue(ctx);
  // This should never happen since we shouldn't be calling this unless we know
  // that it's the appropriate type via the kind checks.
  if (val.IsNothing()) {
    return 0;
  }
  return val.ToChecked();
}
int v8_Value_Bool(ContextPtr ctxptr, PersistentValuePtr valueptr) {
  VALUE_SCOPE(ctxptr);
  v8::Local<v8::Value> value = static_cast<Value*>(valueptr)->Get(isolate);
  v8::Maybe<bool> val = value->BooleanValue(ctx);
  // This should never happen since we shouldn't be calling this unless we know
  // that it's the appropriate type via the kind checks.
  if (val.IsNothing()) {
    return 0;
  }
  return val.ToChecked() ? 1 : 0;
}

unsigned char* v8_Value_Bytes(ContextPtr ctxptr, PersistentValuePtr valueptr, int * length) {
  VALUE_SCOPE(ctxptr);

  v8::Local<v8::Value> value = static_cast<Value*>(valueptr)->Get(isolate);

  v8::ArrayBuffer* bufPtr;

  if (value->IsTypedArray()) {
    bufPtr = *v8::TypedArray::Cast(*value)->Buffer();
  } else if (value->IsArrayBuffer()) {
    bufPtr = v8::ArrayBuffer::Cast(*value);
  } else {
    return NULL;
  }

  if (bufPtr == NULL) {
    return NULL;
  }

  if (length != NULL) {
    *length = bufPtr->GetContents().ByteLength();
  }
  return static_cast<unsigned char*>(bufPtr->GetContents().Data());
}

bool v8_Value_IsFunction(ContextPtr ctxptr, PersistentValuePtr valueptr) {
  VALUE_SCOPE(ctxptr);
  fprintf(stderr,"is func: got value scope, isolate: %p, ctx: %p, valueptr: %p\n", isolate, ctxptr, valueptr);
  v8::Local<v8::Value> value = static_cast<Value*>(valueptr)->Get(isolate);
  fprintf(stderr,"is func: got value\n");
  return value->IsFunction();
}

HeapStatistics v8_Isolate_GetHeapStatistics(IsolatePtr isolate_ptr) {
  if (isolate_ptr == nullptr) {
    return HeapStatistics{};
  }
  v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_ptr);
  v8::HeapStatistics hs;
  isolate->GetHeapStatistics(&hs);
  return HeapStatistics{
    hs.total_heap_size(),
    hs.total_heap_size_executable(),
    hs.total_physical_size(),
    hs.total_available_size(),
    hs.used_heap_size(),
    hs.heap_size_limit(),
    hs.malloced_memory(),
    hs.peak_malloced_memory(),
    hs.does_zap_garbage()
  };
}

void v8_Isolate_LowMemoryNotification(IsolatePtr isolate_ptr) {
  if (isolate_ptr == nullptr) {
    return;
  }
  ISOLATE_SCOPE(static_cast<v8::Isolate*>(isolate_ptr));
  isolate->LowMemoryNotification();
}

ValueTuple v8_Value_PromiseResult(ContextPtr ctxptr, PersistentValuePtr valueptr) {
  VALUE_SCOPE(ctxptr);

  v8::Local<v8::Value> value = static_cast<Value*>(valueptr)->Get(isolate);
  v8::Promise* prom = v8::Promise::Cast(*value);

  if (prom->State() == v8::Promise::PromiseState::kPending) {
    return (ValueTuple){nullptr, nullptr, 0, str_to_cr_str("Promise is pending")};
  }

  v8::Local<v8::Value> res = prom->Result();
  return (ValueTuple){new Value(isolate, res), v8_Value_KindsFromLocal(res), nullptr};
}

uint8_t v8_Value_PromiseState(ContextPtr ctxptr, PersistentValuePtr valueptr) {
  VALUE_SCOPE(ctxptr);

  v8::Local<v8::Value> value = static_cast<Value*>(valueptr)->Get(isolate);
  v8::Promise* prom = v8::Promise::Cast(*value);

  return prom->State();
}

class FileOutputStream : public v8::OutputStream {
 public:
  FileOutputStream(FILE* stream) : stream_(stream) {}

  virtual int GetChunkSize() {
    return 65536;  // big chunks == faster
  }

  virtual void EndOfStream() {}

  virtual WriteResult WriteAsciiChunk(char* data, int size) {
    const size_t len = static_cast<size_t>(size);
    size_t off = 0;

    while (off < len && !feof(stream_) && !ferror(stream_))
      off += fwrite(data + off, 1, len - off, stream_);

    return off == len ? kContinue : kAbort;
  }

 private:
  FILE* stream_;
};

bool v8_Isolate_TakeHeapSnapshot(IsolatePtr isolate_ptr, const char* filename) {
  if (isolate_ptr == nullptr) {
    return false;
  }
  ISOLATE_SCOPE(static_cast<v8::Isolate*>(isolate_ptr))
  FILE* fp = fopen(filename, "w");
  if (fp == NULL) return false;
  const v8::HeapSnapshot* const snap = isolate->GetHeapProfiler()->TakeHeapSnapshot();
  FileOutputStream stream(fp);
  snap->Serialize(&stream, v8::HeapSnapshot::kJSON);
  fclose(fp);
  // Work around a deficiency in the API.  The HeapSnapshot object is const
  // but we cannot call HeapProfiler::DeleteAllHeapSnapshots() because that
  // invalidates _all_ snapshots, including those created by other tools.
  const_cast<v8::HeapSnapshot*>(snap)->Delete();
  return true;
}

void v8_Isolate_MemoryPressureNotification(IsolatePtr isolate_ptr, uint8_t level) {
  v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_ptr);
  v8::MemoryPressureLevel levelToSend;
  if (level == 0) {
    levelToSend = v8::MemoryPressureLevel::kNone;
  } else if (level == 1) {
    levelToSend = v8::MemoryPressureLevel::kModerate;
  } else {
    levelToSend = v8::MemoryPressureLevel::kCritical;
  }
  isolate->MemoryPressureNotification(static_cast<v8::MemoryPressureLevel>(level));
}

} // extern "C"
