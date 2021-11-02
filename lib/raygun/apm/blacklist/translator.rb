module Raygun
  module Apm
    module Blacklist
      class Translator
        class RubyTranslator
          # Foo::Bar#baz
          # Foo::Bar.baz
          # Foo::Bar::baz
          COMMENT = /^#.*/
          ANONYMOUS = /^#<.*>?/
          NAMESPACE = /::/
          NAMESPACE_ONLY = /::$/
          METHOD = /#|\./
          LETTER_CASE = /^[A-Z]/
          LOWER_CASE = /^[a-z]/

          def translate(filter)
            path, method = nil, nil
            if filter !~ COMMENT && filter !~ ANONYMOUS
              if filter.end_with?("#")
                path = filter
              else
                path, method = filter.split(METHOD)
              end
              # .NET fallback
              return if method =~ LETTER_CASE && !method.start_with?("Ruby")
              if path == path.downcase
                method = path
                path = nil
              end

              # .NET fallback
              return if method =~ NAMESPACE
              if path && (_method = path.split(NAMESPACE).last) =~ LOWER_CASE
                method = _method
                path.gsub!(Regexp.compile("::#{method}$"), "")
              end
              [path, method]
            elsif filter =~ ANONYMOUS
              _, klass, method = filter.split(METHOD)
              ["##{klass}", method]
            else
              nil
            end
          end
        end

        # References https://raygun.com/documentation/product-guides/apm/blacklist/
        class DotnetTranslator
          # Foo.Bar::Baz
          COMMENT = /^#/
          NAMESPACE = /\./
          METHOD = /::/
  
          def translate(filter)
            if filter !~ COMMENT
              path, method = nil, nil
              path, method = filter.split(METHOD)
              path.gsub!(NAMESPACE, "::")
              [path, method]
            else
              nil
            end
          end
        end

        def initialize
          @ruby = RubyTranslator.new
          @dotnet = DotnetTranslator.new
        end

        def translate(filter)
          translated = @ruby.translate(filter)
          translated ? translated : @dotnet.translate(filter)
        end
      end
    end
  end
end