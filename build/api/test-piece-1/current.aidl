package some_package {
  parcelable Thing {
    int a;
    int b;
  }

  interface IFoo {
    void CanYouDealWithThisThing(inout some_package.Thing parcel);
    void CanYouDealWithThisSubThing(inout some_package.sub_package.SubThing parcel);
  }

}
package some_package.sub_package {
  parcelable SubThing {
    int a;
    int b;
  }

}
