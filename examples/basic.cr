require "../src/v8"

iso = V8::Isolate.new
ctx = iso.create_context

closure = ->{ puts "Hello, World!" }

logfn = V8::CrystalFunction.new(ctx, "log", V8::FunctionCallback.new do |info|
  puts "in log!"
  puts info.args.map(&.to_s).join(" ")

  next nil
end)

fn2 = V8::CrystalFunction.new(ctx, "blah", V8::FunctionCallback.new do |info|
  pp info
  closure.call

  next nil
end)

global = ctx.global

global.set("cb2", fn2)
global.set("log", logfn)

begin
  ctx.eval <<-JS
    function printData(arguments) {
      log(arguments);
    }
  JS

  ctx.eval <<-JS
    class HelloWorld {
      constructor() {
        this.mainWindow = "Hello, World!"
      }
    }

    let helloWorld = new HelloWorld()
    printData(helloWorld)
  JS

  ctx.eval "cb2(123, 456)"
rescue ex : Exception
  puts "rescued!", ex
end
