#pragma once

#include <atomic>
#include <memory>
#include <cassert>

/*a copy of facebook's wangle's pipelines, but fixed at compile time*/

namespace dasync {

  /*declarations*/
    struct Empty_pipeline_arg;
    class Pipeline_base;
    template<template<typename> class ...Pipes>
    class Pipeline;
 
  namespace impl {
    namespace pipeline {
      /*we need a special tuple to hold variadic templated classes
      
        as it only holds types, it's never defined*/
      template<template<typename> class ...Pipes_>
      struct Pipe_tuple;

      /*declarations*/
      template<typename Pipe_stage_>
      class Pipe_base;

      template<typename Parent, typename After_, typename Before_ = Pipe_tuple<>>
      class Tuple_pipeline;

      namespace util
      {
        /*Count_pipes counts how many classes in a variadic templated class
          (sizeof... wasn't working)*/
        template<template<typename> class ...Pipes>
        struct Count_pipes_;

        template<template<typename> class T, template<typename> class ...Rest>
        struct Count_pipes_<T, Rest...> {
          /*simple remove one, recurse and add 1 to the result*/
          static constexpr size_t size = Count_pipes_<Rest...>::size + 1;
        };

        /*base case*/
        template<>
        struct Count_pipes_<> {
          static constexpr size_t size = 0;
        };

        /*declarations*/
        template<typename P1, typename P2>
        struct Add_;

        template<typename P, typename T>
        struct Inst_;

        template<typename Tuple>
        struct Util;

        /*some utility usings, make the above classes look like functions*/
        /*Add/concatenate two pipe tuples*/
        template<typename P1, typename P2>
        using Add = typename Add_<P1, P2>::type;

        /*Does Head<P><T>, which can't be written without this helper.
          Also allows for P to be empty*/
        template<typename P, typename T>
        using Inst = typename Inst_<P, T>::type;

        /*The first pipe in the tuple*/
        template<typename T>
        using Head = typename Util<T>::Head;
        /*The final pipe in the tuple*/
        template<typename T>
        using Tail = typename Util<T>::Tail;
        /*The tuple without the head*/
        template<typename T>
        using Minus_head = typename Util<T>::Minus_head;
        /*The tuple without the tail*/
        template<typename T>
        using Minus_tail = typename Util<T>::Minus_tail;
        /*The pipeline before us, does some shifting of the before and after
          pipe_tuples*/
        template<typename P, typename A, typename B>
        using Previous = Tuple_pipeline<P, Add<Tail<B>, A>, Minus_tail<B>>;
        /*The pipeline after us, does some shifting of the before and after
          pipe_tuples*/
        template<typename P, typename A, typename B>
        using Next = Tuple_pipeline<P, Minus_head<A>, Add<B, Head<A>>>;
        /*Our child*/
        template<typename P, typename A, typename B>
        using Child = Inst<A, Pipe_base<Tuple_pipeline<P, A, B>>>;

        /*Used to add/concat two pipe tuples*/
        template<template<typename> class ...P1, template<typename> class ...P2>
        struct Add_<Pipe_tuple<P1...>, Pipe_tuple<P2...>> {
          /*concat the pipes*/
          using type = Pipe_tuple<P1..., P2...>;
        };

        /*Used to manipulate pipe tuples*/
        template<template<typename> class First_, template<typename> class ...Rest_>
        struct Util<Pipe_tuple<First_, Rest_...>> {
          /*The first pipe in the tuple*/
          using Head = Pipe_tuple<First_>;
          /*The final pipe in the tuple*/
          using Tail = typename Util<Pipe_tuple<Rest_...>>::Tail;
          /*The tuple without the head*/
          using Minus_head = Pipe_tuple<Rest_...>;
          /*The tuple without the tail*/
          using Minus_tail = Add<Pipe_tuple<First_>, typename Util<Pipe_tuple<Rest_...>>::Minus_tail>;
        };

        /*actual base case since we static_assert that we are given at least 1 pipe*/
        template<template<typename> class First_>
        struct Util<Pipe_tuple<First_>> {
          /*The first pipe in the tuple*/
          using Head = Pipe_tuple<First_>;
          /*The final pipe in the tuple*/
          using Tail = Pipe_tuple<First_>;
          /*The tuple without the head*/
          using Minus_head = Pipe_tuple<>;
          /*The tuple without the tail*/
          using Minus_tail = Pipe_tuple<>;
        };

        /*base case needed for compilation to succeed*/
        template<>
        struct Util<Pipe_tuple<>> {
          using Head = Pipe_tuple<>;
          using Tail = Pipe_tuple<>;
          using Minus_head = Pipe_tuple<>;
          using Minus_tail = Pipe_tuple<>;
        };

        /*Use partial template specialization to do Head<tuple><T>*/
        template<template<typename> class Pipe_, template<typename> class ...Rest_, typename T>
        struct Inst_<Pipe_tuple<Pipe_, Rest_...>, T> {
          using type = Pipe_<T>;
        };

        /*needed for compilation to succeed*/
        template<typename T>
        struct Inst_<Pipe_tuple<>, T> {
          using type = void;
        };
      }

      /*the class passed to each pipe to derive from*/
      template<typename Pipe_stage_>
      class Pipe_base {
      protected:
        /*typedef so we can see what Pipe_stage_ is from outside*/
        using Pipe_stage =  Pipe_stage_;

        /*if our parent has a previous, this will compile*/
        template<typename T = typename Pipe_stage::Prev::Child>
        T& get_prev() {
          /*cast ourselves to our parent, get the previous pipeline, get its child*/
          return static_cast<Pipe_stage&>(*this).previous_pipeline().get_child();
        }

        /*if our parent has a next, this will compile*/
        template<typename T = typename Pipe_stage::Next::Child>
        T& get_next() {
          /*cast ourselves to our parent, get the next pipeline, get its child*/
          return static_cast<Pipe_stage&>(*this).next_pipeline().get_child();
        }

        /*get our parent, must be like this to make it compile*/
        template<typename T = typename Pipe_stage::Parent>
        T& get_parent() {
          return static_cast<T&>(*this);
        }

        /*if our stage doesn't define this, lets our parent still call it when closing*/
        static void close() {

        }

        /*if our stage doesn't define this, lets our parent still cal it when starting*/
        static void start() {

        }
      };

      /*A pipeline,
      
      Parent_ is the dasync::Pipeline,
      A is the Pipe_tuple of pipes After us,
      B is the Pipe_tuple of pipes Before us*/
      template<typename Parent_, typename A, typename B>
      class Tuple_pipeline :
        protected util::Child<Parent_, A, B>,
        public util::Next<Parent_, A, B> {
      public:
        /*let things outside us see these*/
        using Parent = Parent_;
        using Child = util::Child<Parent, A, B>;

        /*forward the first arg to our child, the rest go to the next pipeline*/
        template<typename First, typename ...Rest>
        Tuple_pipeline(First&& first, Rest&&... rest) :
          Child{ std::forward<First>(first) },
          Next{ std::forward<Rest>(rest)... } {

        }

        /*if given an Empty_pipeline_arg, nothing goes to the child,
          rest go to the next pipeline*/
        template<typename ...Rest>
        Tuple_pipeline(Empty_pipeline_arg const&, Rest&&... rest) :
          Child{},
          Next{ std::forward<Rest>(rest)... } {

        }

        /*nothing goes anywhere*/
        Tuple_pipeline() :
          Child{},
          Next{} {

        }

        /*let our next, previous and child's pipe_base play with our protected functions
          which are used for moving backwards and forwards through the pipeline*/
        friend util::Next<Parent, A, B>;
        friend util::Previous<Parent, A, B>;
        friend Pipe_base<Tuple_pipeline<Parent, A, B>>;

        /*returns our child, as the top-most tuple_pipeline is the left-most
          and therefore the 'front' */
        Child& get_front() {
          return get_child();
        }

        /*using for the get_back*/
        using util::Next<Parent_, A, B>::get_back;

        /*must be public so our next and previous's child's pipe_bases can access*/
        Child& get_child() {
          return static_cast<Child&>(*this);
        }
      protected:
        /*helper typedefs*/
        using Next = util::Next<Parent, A, B>;
        using Prev = util::Previous<Parent, A, B>;
        using Me = Tuple_pipeline<Parent, A, B>;

        /*if we don't have nothing before us, we can get the previous pipeline*/
        template<typename T = Prev>
        std::enable_if_t<!std::is_same<B, Pipe_tuple<>>::value, T&> previous_pipeline() {
          /*just a cast since we know it exists and it is derived from us*/
          return static_cast<T&>(*this);
        }

        /*if we don't have nothing after us, we can get the next pipeline*/
        template<typename T = Next>
        std::enable_if_t<!std::is_same<util::Minus_head<A>, Pipe_tuple<>>::value, T&> next_pipeline() {
          /*just a cast since we know it exists and we derive from it*/
          return static_cast<T&>(*this);
        }

        /*get the parent*/
        Parent& parent() {
          /*just a cast since it must exist and it derives from us somehow*/
          static_cast<Parent&>(*this);
        }

        /*used when calling down to close the pipeline*/
        void internal_close() {
          /*call the child's close*/
          Child::close();
          /*call the next pipeline's close*/
          Next::internal_close();
        }

        /*used when calling down to open the pipelin*/
        void internal_start() {
          /*call the child's start*/
          Child::start();
          /*call the next pipeline's start*/
          Next::internal_start();
        }
      private:
      };

      /*base case for the pipeline*/
      template<typename P, typename B>
      class Tuple_pipeline<P, Pipe_tuple<>, B> {
      public:
        /*helper typedef*/
        using Prev = util::Previous<P, Pipe_tuple<>, B>;

        /*since we are the last, we need to define get_back
        
          only we define it, so it will be visible.
          
          A bit of repitition using the inst helper, but it plays nicely
          with intellisense unlike:
          template<T = typename Prev::Child> T& get_back() */
        template<typename T = Prev>
        util::Inst<util::Tail<B>, Pipe_base<T>>& get_back() {
          /*since we don't allow pipelines with no pipes,
            there must be at least one pipeline before us,
            get it and then its child*/
          return static_cast<Prev&>(*this).get_child();
        }
      protected:
        /*base cases for closing/starting*/
        static void internal_close() {}
        static void internal_start() {}
      };
    }
  }

  /*states the pipeline can be in*/
  enum class Pipeline_state {
    initialized, /*constructed but not started*/
    starting, /*start has been called but hasn't returned*/
    running, /*start has returned and close hasn't been called*/
    closing, /*close has been called but hasn't returned*/
    closed /*close has returned*/
  };

  /*used to signify that a pipe shouldn't recieve any constructor parameters*/
  struct Empty_pipeline_arg {};

  /*use this when needing to use an Empty_pipeline_arg*/
  extern const Empty_pipeline_arg empty_pipeline_arg;

  /*The base of the pipeline, allows for a container of different pipelines*/
  class Pipeline_base : 
    public std::enable_shared_from_this<Pipeline_base> {
  public:
    /*start this pipeline*/
    virtual void start() = 0;

    /*close this pipeline*/
    virtual void close() = 0;

    /*get the state of the pipeline*/
    Pipeline_state state() const;

    /*virtual destructor for nice destruction*/
    virtual ~Pipeline_base();
  protected:
    /*this class must be derived from, simple constructor*/
    Pipeline_base();

    /*used internally when close is called to change to closing
    
      returns 0 on we can close
             -1 on we can't close (someone got to it before us)*/
    int start_close();
    /*used internally when close is about to return to change to closed*/
    void finish_close();
    /*used internally when start is called to change to starting
    
      return 0 on we can start
            -1 on we can't start (someone got to it before us)*/
    int start_start();
    /*used internally when start is about to return to change to running*/
    void finish_start();

  private:
    /*don't move, don't copy*/
    Pipeline_base(Pipeline_base const&) = delete;
    Pipeline_base(Pipeline_base&&) = delete;

    Pipeline_base& operator=(Pipeline_base const&) = delete;
    Pipeline_base& operator=(Pipeline_base&&) = delete;

    /*our state*/
    std::atomic<Pipeline_state> state_;
  };

  /*the user facing side of the pipeline,
  
    must be given a non-zero amount of pipes
  
    Currently must be created with a std::make_shared. Since pipe local storage is 
    part of the struct, destroying the struct while a pipe is still closing up may
    lead to pipes accessing deleted memory. Close could be required to be sync,
    but is decided against. (Current pipeline implementation does not use the 
    std::enable_shared_from_this, so this may be done by the user. std::make_shared
    is safer however).*/
  template<template<typename> class ...Pipes>
  class Pipeline final :
    public Pipeline_base,
    private impl::pipeline::Tuple_pipeline<Pipeline<Pipes...>, impl::pipeline::Pipe_tuple<Pipes...>> {
  public:
    /*ensures we were given a non-zero amount of pipes*/
    static_assert(impl::pipeline::util::Count_pipes_<Pipes...>::size > 0,
      "no point having a pipeline with no pipes");

    /*helper typedef*/
    using Internal_pipeline = impl::pipeline::Tuple_pipeline<Pipeline<Pipes...>, impl::pipeline::Pipe_tuple<Pipes...>>;

    /*constructor, passes it onto the implementation*/
    template<typename ...Args>
    Pipeline(Args&&... args) :
      Internal_pipeline{ std::forward<Args>(args)... } {

    }

    /*start the pipeline*/
    virtual void start() override final {
      /*if we can start (we haven't been started), start*/
      if (Pipeline_base::start_start() == 0) {
        Internal_pipeline::internal_start();

        Pipeline_base::finish_start();
      }
    }

    /*close the pipeline*/
    virtual void close() override final {
      /*if we can close (we haven't been closed), close*/
      if (Pipeline_base::start_close() == 0) {
        Internal_pipeline::internal_close();

        Pipeline_base::finish_close();
      }
    }

    /*usings to expose the get_front and get_back*/
    using Internal_pipeline::get_front;
    using Internal_pipeline::get_back;

    /*on destruction, close. If we are already closed this will do nothing
      (see inside close())*/
    ~Pipeline() {
      close();
    }
  };
}