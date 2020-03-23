local json = require 'dkjson'

function base(param)
  print("base param:".. param)

  local dvalue = json.decode(param)
  if(dvalue.event ~= nil) then
    pelagia.EventSend(dvalue.event, "hello")
  end

  --TableClear and get
  local error = 1
  pelagia.TableClear("t0")
  local r = pelagia.Get("t0", "a")
  if (r == nil) then
    error = 0;
  end

  if error == 1 then
    print("fail TableClear and get!\n")
    return 1;
  end

  --set and get
  error = 1;
  pelagia.Set("t0", "a", "b")
  r = pelagia.Get("t0", "a")
  if ( r == "b") then
    error = 0;
  end

  if error == 1 then
    print("fail set and get!\n")
    return 1;
  end

  --set and rand
  error = 1;
  r = pelagia.Rand("t0")
  if ( r == "b") then
    error = 0;
  end

  if error == 1 then
    print("fail set and get!\n")
    return 1;
  end

  --set and length
  r = pelagia.Length("t0")
  if(r ~= 1) then
    print("fail set and length!\n");
    return 1;
  end

    --set and IsKeyExist
    r = pelagia.IsKeyExist("t0", "a")
    if(r ~= 1) then
      print("fail set and IsKeyExist!\n");
      return 1;
    end

  --del and get
  error = 1;
  pelagia.Del("t0", "a")
  r = pelagia.Get("t0", "a")
  if ( r == nil) then
    error = 0;
  end

  if error == 1 then
    print("fail del and get!\n")
    return 1;
  end

  --setIfNoExit and get
  error = 1;
  pelagia.SetIfNoExit("t0", "a", "b")
  r = pelagia.Get("t0", "a")
  if ( r == "b") then
    error = 0;
  end

  if error == 1 then
    print("fail setIfNoExit and get!\n")
    return 1;
  end

  --rename and get
  error = 1;
  pelagia.Rename("t0", "a", "c")
  r = pelagia.Get("t0", "c")
  if ( r == "b") then
    error = 0;
  end

  if error == 1 then
    print("fail rename and get!\n")
    return 1;
  end

  --multiset and get
  error = 1;
  local kv= {
    c1 = "c",
    c2 = "b",
    c3 = "c",
    c4 = "b",
    c5 = "b",
    c6 = "c",
  }

  local kvjson = json.encode(kv);
  pelagia.MultiSet("t0", kvjson)
  r = pelagia.Get("t0", "c")
  if ( r == "b") then
    error = 0;
  end

  if error == 1 then
    print("fail rename and get!\n")
    return 1;
  end

  --multiset and limite
  error = 1;
  local rjson = pelagia.Limite("t0", "c3", 2, 2);
  kv = json.decode(rjson);
  if ( kv.c1 == "c") then
    error = 0;
  end

  if error == 1 then
    print("fail multiset and limite!\n")
    return 1;
  end

  --mulitiset and order
  error = 1;
  rjson = pelagia.Order("t0", 0, 1);
  kv = json.decode(rjson);
  if ( kv.a == "b") then
    error = 0;
  end

  if error == 1 then
    print("fail MultiSet and order at first!\n")
    return 1;
  end

  error = 1;
  rjson = pelagia.Order("t0", 1, 1);
  kv = json.decode(rjson);
  if ( kv.c6 == "c") then
    error = 0;
  end

  if error == 1 then
    print("fail MultiSet and order at tail!\n")
    return 1;
  end

  --mulitiset and rang
  error = 1;
  rjson = pelagia.Rang("t0", "c1", "c1");
  kv = json.decode(rjson);
  if ( kv.c1 ~= nil) then
    error = 0;
  end

  if error == 1 then
    print("fail MultiSet and rang!\n")
    return 1;
  end

  --mulitiset and MultiGet
  error = 1;
  local kv = {
    "c1"
  }
  local kjson = json.encode(kv)
  kvjson = pelagia.MultiGet("t0", kjson)
  kv = json.decode(kvjson);
  if( kv.c1 ~= nil) then
    error = 0;
  end

  if error == 1 then
    print("fail MultiSet and MultiGet!\n")
    return 1;
  end

  --mulitiset and Pattern
  error =  1;
  local kvjson = pelagia.Pattern("t0", "c1", "c1", "?1")
  kv = json.decode(kvjson);
  if( kv.c1 ~= nil) then
    error = 0;
  end

  if error == 1 then
    print("fail MultiSet and Pattern!\n")
    return 1;
  end

  --//set////////////////////////////////////////////////

  --TableClear and get
  local error = 1
  pelagia.TableClear("t1")
  if (1 == pelagia.SetIsKeyExist("t1", "a", "b")) then
    print("fail TableClear and get!\n")
    return 1;
  end

  --SetAdd and SetIsKeyExist
  pelagia.SetAdd("t1", "a", "b")
  if (0 == pelagia.SetIsKeyExist("t1", "a", "b")) then
    print("fail TableClear and get!\n")
    return 1;
  end

  --SetAdd and SetRang
  error = 1;
  rjson = pelagia.SetRang("t1", "a", "b", "b");
  kv = json.decode(rjson);
  if ( kv.b ~= nil) then
    error = 0;
  end

  if error == 1 then
    print("fail MultiSet and rang!\n")
    return 1;
  end

  --SetAdd and SetRangCount
  if (1 ~= pelagia.SetRangCount("t1", "a", "b", "b")) then
    print("fail SetAdd and SetRangCount!\n")
    return 1;
  end

  --SetAdd and SetLimit
  error = 1;
  rjson = pelagia.SetLimite("t1", "a", "b", 1, 1);
  kv = json.decode(rjson);
  if ( kv.b ~= nil) then
    error = 0;
  end

  if (error == 1) then
    print("fail SetAdd and SetLimite!\n")
    return 1;
  end 

  --SetAdd and SetLength
  if (1 ~= pelagia.SetLength("t1", "a")) then
    print("fail SetAdd and setlength!\n")
    return 1;
  end

  --SetAdd and SetMembers
  error = 1;
  rjson = pelagia.SetMembers("t1", "a");
  kv = json.decode(rjson);
  if ( kv.b ~= nil) then
    error = 0;
  end

  if error == 1 then
    print("fail SetAdd and SetMembers!\n")
    return 1;
  end
  
  --SetAdd and SetRand
  error = 1;
  local value = pelagia.SetRand("t1", "a");

  if ( value == "b" ) then
    error = 0;
  end

  if error == 1 then
    print("fail setadd and setrand!\n")
    return 1;
  end

  --SetAdd and SetMove
  pelagia.SetMove("t1", "a", "c", "b");
  if 0 ~= pelagia.SetIsKeyExist("t1", "a", "b") then
    print("fail SetAdd and SetMove!\n")
    return 1;
  end

  if 0 == pelagia.SetIsKeyExist("t1", "c", "b") then
    print("fail SetAdd and SetMove!\n")
    return 1;
  end

  --SetAdd and SetPop
  error = 1;
  value = pelagia.SetPop("t1", "c");
  if value == "b" then
      error = 0;
  end

  if error == 1 then
    print("fail SetAdd and SetPop!\n");
    return 1;
  end

  --SetAdd and SetDel
  pelagia.SetAdd("t1", "a", "b")
  local kv= {
    "b"
  }
  kjson = json.encode(kv);

  pelagia.SetDel("t1", "a", kjson);
  if 1 == pelagia.SetIsKeyExist("t1", "a", "b") then
    print("fail SetAdd and SetaDel!\n")
    return 1;
  end

  --SetAdd and SetUion
  error = 1;
  pelagia.SetAdd("t1", "a", "b")
  pelagia.SetAdd("t1", "c", "d")
  local kv = {
    "a",
    "c"
  }

  kjson = json.encode(kv);
  rjson = pelagia.SetUion("t1", kjson);
  kv = json.decode(rjson);
  if kv.b ~= nil then
      error = 0;
  end

  if error == 1 then
    print("fail SetAdd and Uion!\n");
    return 1;
  end

  --SetAdd and SetInter
  error = 1;
  pelagia.SetAdd("t1", "a", "e")
  pelagia.SetAdd("t1", "c", "e")
  local kv = {
    "a",
    "c"
  }

  kjson = json.encode(kv);
  rjson = pelagia.SetInter("t1", kjson);
  kv = json.decode(rjson);
  if kv.e ~= nil then
      error = 0;
  end

  if error == 1 then
    print("fail SetAdd and Inter!\n");
    return 1;
  end

  --SetAdd and SetDiff
  error = 1;
  local kv = {
    "a",
    "c"
  }

  kjson = json.encode(kv);
  rjson = pelagia.SetDiff("t1", kjson);
  kv = json.decode(rjson);
  if kv.d ~= nil then
      error = 0;
  end

  if error == 1 then
    print("fail SetAdd and Diff!\n");
    return 1;
  end

  --SetAdd and SetUionStore
  error = 1;
  local kv = {
    "a",
    "c"
  }

  kjson = json.encode(kv);
  pelagia.SetUionStore("t1", kjson, "u");
  if 1 == pelagia.SetIsKeyExist("t1", "u", "d") then
      error = 0;
  end

  if error == 1 then
    print("fail SetAdd and SetUionStore!\n");
    return 1;
  end

  --SetAdd and SetInterStore
  error = 1;
  local kv = {
    "a",
    "c"
  }

  kjson = json.encode(kv);
  pelagia.SetInterStore("t1", kjson, "i");
  if 1 == pelagia.SetIsKeyExist("t1", "i", "e") then
      error = 0;
  end

  if error == 1 then
    print("fail SetAdd and SetUionStore!\n");
    return 1;
  end

  --SetAdd and SetDiffStore
  error = 1;
  local kv = {
    "a",
    "c"
  }

  kjson = json.encode(kv);
  pelagia.SetDiffStore("t1", kjson, "f");
  kv = json.decode(rjson);
  if 1 == pelagia.SetIsKeyExist("t1", "f", "d") then
      error = 0;
  end

  if error == 1 then
    print("fail SetAdd and SetDiffStore!\n");
    return 1;
  end

  print("job all pass!\n");
  return 1;
end