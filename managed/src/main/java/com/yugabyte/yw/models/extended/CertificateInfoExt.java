package com.yugabyte.yw.models.extended;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import javax.persistence.Column;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@ApiModel(description = "SSL certificate used by the universe")
public class CertificateInfoExt {

  @JsonUnwrapped private CertificateInfo certificateInfo;

  @ApiModelProperty(
      value =
          "The certificate's creation date. <b style=\"color:#ff0000\">Deprecated since "
              + "YBA version 2.17.2.0.</b> Use stateDateIso instead",
      accessMode = READ_WRITE)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.17.2.0")
  private Date startDate;

  @ApiModelProperty(
      value =
          "The certificate's expiry date. <b style=\"color:#ff0000\">Deprecated since "
              + "YBA version 2.17.2.0.</b> Use expirtyDateIso instead",
      accessMode = READ_WRITE)
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.17.2.0")
  private Date expiryDate;
}
