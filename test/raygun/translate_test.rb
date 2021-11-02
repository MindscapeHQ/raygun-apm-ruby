require "test_helper"

class Raygun::TranslateTest < Raygun::Test
  def setup
    @translator = Raygun::Apm::Blacklist::Translator.new
  end

  def test_ruby_syntax_translator
    # tuple represents path, method
    assert_equal ["Foo::Bar", "baz"], @translator.translate("Foo::Bar#baz")
    assert_equal ["Foo::Bar",  "baz"], @translator.translate("Foo::Bar.baz")
    assert_equal ["Deeply::Nested::Foo::Bar", "baz"], @translator.translate("Deeply::Nested::Foo::Bar.baz")
    assert_equal ["Bar", "baz"], @translator.translate("Bar#baz")
    assert_equal ["Bar", "baz"], @translator.translate("Bar.baz")
    assert_equal ["Bar#", nil], @translator.translate("Bar#")
    assert_equal ["Foo::Bar", nil], @translator.translate("Foo::Bar")
    # Uppercase without method qualification is considered a class
    assert_equal ["Foo", nil], @translator.translate("Foo")
    # Lowcase is considered a method. Not 100% but as a general 99%tile rule
    assert_equal [nil, "foo"], @translator.translate("foo")
    # Anonymous classes, modules and refinements
    assert_equal ["#<Module:Foo>", "bar"], @translator.translate("#<Module:Foo>#bar")
    assert_equal ["#<refinement:Mod>", "bar"], @translator.translate("#<refinement:Mod>#bar")
    assert_equal ["#<ActiveRecord::AttributeMethods::GeneratedAttributeMethods:0x000055859335c758>", "__temp__f6074796f6e637"], @translator.translate("#<ActiveRecord::AttributeMethods::GeneratedAttributeMethods:0x000055859335c758>.__temp__f6074796f6e637")
    assert_nil @translator.translate("#Foo::Bar")
    assert_equal ["IO::EWOULDBLOCKWaitWritable", nil], @translator.translate("IO::EWOULDBLOCKWaitWritable")
    assert_equal ["Net::", nil], @translator.translate("Net::")
    assert_equal ["Raygun::Apm::", nil], @translator.translate("Raygun::Apm::")
    assert_equal ["Kernel", "sleep"], @translator.translate("Kernel.sleep")
    assert_equal ["Raygun::Apm::Rails::Middleware", "Ruby_APM_profiler_trace"], @translator.translate("Raygun::Apm::Rails::Middleware#Ruby_APM_profiler_trace")
  end

  # References https://raygun.com/documentation/product-guides/apm/blacklist/
  def test_dotnet_syntax_translator
    # tuple represents path, method
    assert_equal ["Foo::Bar",  "baz"], @translator.translate("Foo.Bar::baz")
    assert_equal ["Foo::Bar",  nil], @translator.translate("Foo.Bar")
    assert_equal ["Foo::Bar::",  nil], @translator.translate("Foo.Bar.")
    assert_nil @translator.translate("#Foo.Bar::baz")
    assert_equal ["FilterReason",  "to_dc"], @translator.translate("FilterReason::to_dc")
    assert_equal ["InvestigationWorkflow",  "closed_transitions"], @translator.translate("InvestigationWorkflow::closed_transitions")
    assert_equal ["Foo::InvestigationWorkflow",  "closed_transitions"], @translator.translate("Foo::InvestigationWorkflow::closed_transitions")
  end
end
