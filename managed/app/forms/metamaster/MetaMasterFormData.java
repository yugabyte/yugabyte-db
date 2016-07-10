package forms.metamaster;

import java.util.List;

import models.metamaster.MetaMasterEntry;
import play.data.validation.Constraints;

public class MetaMasterFormData {
  // Information about the masters.
  @Constraints.Required()
  public List<MetaMasterEntry.MasterInfo> masters;
}
