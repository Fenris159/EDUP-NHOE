#pragma once
#include <charconv>
#include <cstdint>
#include <string_view>

namespace edtrace {
// Only the first five /proc/self/maps fields are read. Paths/command lines are
// unnecessary: st_dev + st_ino identify the installed PE mapping exactly.
inline bool NativeXrMappedFile(std::string_view line,std::uint64_t expected_major,
                               std::uint64_t expected_minor,std::uint64_t expected_inode) noexcept {
  const auto field=[&](std::string_view& value) {
    const auto begin=line.find_first_not_of(" \t");
    if(begin==std::string_view::npos) return false;
    line.remove_prefix(begin);
    const auto end=line.find_first_of(" \t");
    value=line.substr(0,end);
    if(end==std::string_view::npos) line={}; else line.remove_prefix(end);
    return true;
  };
  std::string_view value,device,inode;
  for(unsigned i=0;i<3;++i) if(!field(value)) return false;
  if(!field(device)||!field(inode)||!expected_inode) return false;
  const auto colon=device.find(':');
  if(colon==std::string_view::npos) return false;
  const auto parse=[](std::string_view text,std::uint64_t& number,int base) {
    const auto result=std::from_chars(text.data(),text.data()+text.size(),number,base);
    return !text.empty()&&result.ec==std::errc{}&&result.ptr==text.data()+text.size();
  };
  std::uint64_t major=0,minor=0,number=0;
  return parse(device.substr(0,colon),major,16)&&parse(device.substr(colon+1),minor,16)&&
    parse(inode,number,10)&&major==expected_major&&minor==expected_minor&&number==expected_inode;
}
}
